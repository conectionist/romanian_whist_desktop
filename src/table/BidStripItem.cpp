#include "BidStripItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"
#include "paint/TextRuns.h"
#include "table/HudText.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"

#include <QCoreApplication>
#include <QFont>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLinearGradient>
#include <QPainter>

#include <vector>

namespace
{

// page.html's .bidstrip, in table units, border-box: a border of 1, padding of 16
// above, 22 either side and 18 below, and everything centred.
constexpr qreal borderWidth = 1.0;
constexpr qreal paddingTop = 16.0;
constexpr qreal paddingX = 22.0;
constexpr qreal paddingBottom = 18.0;

constexpr qreal contentWidth = tablespace::bidStripWidth - 2.0 * (borderWidth + paddingX);

// .bidstrip h2: Marcellus 25, 12 above the bids.
constexpr qreal titleSize = 25.0;
constexpr qreal titleGap = 12.0;

// .bidstrip .opt: 58 x 62, 10 apart, a border of 1 rounded 8, the number in
// Marcellus 28, and a shadow of 0 4px 10px.
constexpr qreal optWidth = 58.0;
constexpr qreal optHeight = 62.0;
constexpr qreal optGap = 10.0;
constexpr qreal optRadius = 8.0;
constexpr qreal optBorder = 1.0;
constexpr qreal numeralSize = 28.0;
constexpr QPointF optShadowOffset(0.0, 4.0);
constexpr qreal optShadowBlur = 10.0;

// .opt:hover: 0 0 0 2px gold-bright and 0 0 16px, in place of the shadow.
constexpr qreal ringWidth = 2.0;
constexpr qreal glowBlur = 16.0;

// .bidstrip p: 12 below the bids, 16, muted.
constexpr qreal noteGap = 12.0;
constexpr qreal noteSize = 16.0;

struct Styles
{
    textruns::Style title;
    textruns::Style numeral;
    textruns::Style struck;
    textruns::Style note;
};

Styles stylesFor(const Theme::TablePalette& palette)
{
    Styles styles;

    styles.title = hudtext::style(fonts::heading(), titleSize, palette.cream, palette);
    styles.numeral = hudtext::style(fonts::heading(), numeralSize, palette.cream, palette);

    // .opt:disabled: text-decoration: line-through, in its own dull colour.
    QFont struck = fonts::heading();
    struck.setStrikeOut(true);

    styles.struck = hudtext::style(struck, numeralSize, palette.bidDisabledText, palette);
    styles.note = hudtext::style(fonts::body(), noteSize, palette.muted, palette);

    return styles;
}

// The styles the strip is MEASURED in. The colours change no width, so any
// palette's will do.
const Styles& measuring()
{
    static const Styles styles = stylesFor(Theme::dark().table);
    return styles;
}

// The question and the note, broken into lines no wider than the strip's content.
// A translation may need two lines where English needs one.
std::vector<textruns::Line> linesOf(const QString& text, const textruns::Style& style)
{
    return textruns::wrap({ textruns::Run{ text } }, style, contentWidth);
}

// Where everything goes, from the top of the strip down.
struct Layout
{
    std::vector<textruns::Line> question;
    qreal questionTop = 0.0;
    qreal questionLine = 0.0;

    qreal bidsTop = 0.0;

    std::vector<textruns::Line> note;
    qreal noteTop = 0.0;
    qreal noteLine = 0.0;

    qreal height = 0.0;
};

Layout layOut(const bidstrip::Fields& fields, const Styles& styles)
{
    Layout layout;

    layout.question = linesOf(fields.question, styles.title);
    layout.questionTop = borderWidth + paddingTop;
    layout.questionLine = textruns::metrics(styles.title).lineSpacing;

    layout.bidsTop = layout.questionTop
                   + static_cast<qreal>(layout.question.size()) * layout.questionLine + titleGap;

    layout.note = linesOf(fields.note, styles.note);
    layout.noteTop = layout.bidsTop + optHeight + noteGap;
    layout.noteLine = textruns::metrics(styles.note).lineSpacing;

    layout.height = layout.noteTop + static_cast<qreal>(layout.note.size()) * layout.noteLine
                  + paddingBottom + borderWidth;

    return layout;
}

// Each line centred across the strip, one under the other from `top`.
void paintCentred(QPainter& painter, const std::vector<textruns::Line>& lines,
                  const textruns::Style& style, qreal top, qreal line)
{
    for(const textruns::Line& text : lines)
    {
        const qreal left = (tablespace::bidStripWidth - textruns::width(text, style)) / 2.0;

        textruns::paint(painter, QPointF(left, textruns::baselineIn(style, top, line)), text, style);
        top += line;
    }
}

enum class Look
{
    Plain,
    Lit,
    Refused
};

// One bid's button, in CSS's paint order: its outer shadows, its background, its
// border, and its number on top.
void paintBid(QPainter& painter, const QRectF& rect, unsigned int bet, Look look,
              const Styles& styles, const Theme::TablePalette& palette)
{
    const QPainterPath outline = paths::rounded(rect, optRadius);

    // THE OUTER SHADOWS, the last listed lowest: .opt:hover's ring over its glow,
    // both in place of the plain shadow. A refused bid has none at all.
    if(look == Look::Plain)
        shadows::drawShapeShadow(painter, outline, optShadowOffset, optShadowBlur, palette.bidShadow);

    if(look == Look::Lit)
    {
        shadows::drawShapeShadow(painter, outline, QPointF(), glowBlur, palette.bidGlow);

        painter.fillPath(paths::roundedBand(rect.adjusted(-ringWidth, -ringWidth, ringWidth, ringWidth),
                                            optRadius + ringWidth, rect, optRadius),
                         palette.goldBright);
    }

    // The background over the padding box, painted under the border as CSS paints
    // one: the buttons' green from top to bottom, or the refused bid's flat brown.
    const QRectF padding = rect.adjusted(optBorder, optBorder, -optBorder, -optBorder);

    if(look == Look::Refused)
    {
        painter.fillPath(outline, palette.bidDisabledFill);
    }
    else
    {
        QLinearGradient green(padding.topLeft(), padding.bottomLeft());
        green.setColorAt(0.0, palette.buttonTop);
        green.setColorAt(1.0, palette.buttonBottom);
        painter.fillPath(outline, green);
    }

    QColor border = palette.buttonBorder;

    if(look == Look::Lit)
        border = palette.goldBright;
    else if(look == Look::Refused)
        border = palette.bidDisabledBorder;

    painter.fillPath(paths::roundedBand(rect, optRadius, padding, optRadius - optBorder), border);

    // The number, centred both ways on its line box, as a button centres its
    // content.
    const textruns::Style& style = look == Look::Refused ? styles.struck : styles.numeral;
    const textruns::Line numeral{ textruns::Run{ QString::number(bet) } };
    const qreal line = textruns::metrics(style).lineSpacing;

    textruns::paint(painter,
                    QPointF(rect.center().x() - textruns::width(numeral, style) / 2.0,
                            textruns::baselineIn(style, rect.center().y() - line / 2.0, line)),
                    numeral, style);
}

}

bidstrip::Fields bidstrip::fieldsFor(const BetPrompt& prompt, const GameSnapshot& snapshot)
{
    Fields fields;

    fields.question = QCoreApplication::translate("bidstrip", "How many tricks will you take?");

    // The hand's size IS the trick count and so the highest legal bid. Narrowed
    // here rather than carried as a size_t, because it is about to become a number
    // of buttons and a bid, and both are unsigned int - the type
    // IMoveProvider::makeBet() returns.
    fields.highest = static_cast<unsigned int>(prompt.handSize);

    fields.forbidden = prompt.forbiddenBet;

    // ENGAGED MEANS YOU BID LAST. Nothing here infers turn order or counts what
    // others have bid - getForbiddenBet() is only ever filled for the seat that
    // closes the bidding, so the presence of the value is the fact, and the
    // sentence can say so outright.
    //
    // One arg() taking two rather than two chained, the rule SnapshotLog.cpp
    // states: arg() fills the lowest placeholder still standing, including one
    // that arrived inside a string it just substituted.
    if(prompt.forbiddenBet)
    {
        fields.note = QCoreApplication::translate("bidstrip",
                                                  "You bid last. %1 would make the bids add up to %2, "
                                                  "and somebody has to be wrong.")
                          .arg(QString::number(*prompt.forbiddenBet), QString::number(fields.highest));

        return fields;
    }

    // OTHERWISE HOW THE BIDS STAND, the mock-up's other sentence, from the frame the
    // engine published as it asked you (GameBridge::onBetRequested()): the bids
    // already in and what they add up to, against this round's tricks. Singular
    // or plural on the trick count, as the scoreboard's own tension line is.
    const QString placed = QString::number(snapshot.betsPlaced);
    const QString total = QString::number(snapshot.betSum);
    const QString tricks = QString::number(fields.highest);

    fields.note = (fields.highest == 1
                       ? QCoreApplication::translate("bidstrip", "%1 bid so far, totalling %2 of %3 trick.")
                       : QCoreApplication::translate("bidstrip", "%1 bid so far, totalling %2 of %3 tricks."))
                      .arg(placed, total, tricks);

    return fields;
}

bool bidstrip::offers(const Fields& fields, unsigned int bet)
{
    // A FORBIDDEN BID OF ZERO IS STILL FORBIDDEN, which is why this tests the
    // engagement and not the value: zero is a legal bid, so an optional holding 0
    // must not read as "nothing forbidden".
    return bet <= fields.highest && !(fields.forbidden && *fields.forbidden == bet);
}

bidstrip::KeyAnswer bidstrip::answerKey(int key, Qt::KeyboardModifiers modifiers, const Fields& fields)
{
    if((modifiers & ~Qt::KeypadModifier) != Qt::NoModifier)
        return {};

    if(key < Qt::Key_0 || key > Qt::Key_9)
        return {};

    const unsigned int bet = static_cast<unsigned int>(key - Qt::Key_0);

    // Not a digit this strip offers: passed on rather than swallowed, so a key the
    // window binds still reaches it.
    if(bet > fields.highest)
        return {};

    // A REFUSED DIGIT IS IGNORED, which section 15.1 asks for by name, and through
    // offers() - the one rule about what is clickable, rather than two that can
    // disagree.
    if(!offers(fields, bet))
        return { KeyAnswer::Kind::Refuse, bet };

    return { KeyAnswer::Kind::Bid, bet };
}

QSizeF bidstrip::sizeFor(const Fields& fields)
{
    return QSizeF(tablespace::bidStripWidth, layOut(fields, measuring()).height);
}

namespace
{

// The first bid's button, its row's top at `bidsTop`. 0..highest INCLUSIVE, side by
// side and centred: an 8-card round offers nine bids, and bidding zero is a real
// choice rather than the absence of one.
QRectF firstButton(const bidstrip::Fields& fields, qreal bidsTop)
{
    const qreal count = static_cast<qreal>(fields.highest) + 1.0;
    const qreal row = count * optWidth + (count - 1.0) * optGap;

    return QRectF((tablespace::bidStripWidth - row) / 2.0, bidsTop, optWidth, optHeight);
}

// The same, measured: a whole layout, the question's wrapping included. The item
// does this once per change of what it says (BidStripItem::refresh()) and keeps
// the answer, because its hit test runs on every move of the cursor.
QRectF measuredFirstButton(const bidstrip::Fields& fields)
{
    return firstButton(fields, layOut(fields, measuring()).bidsTop);
}

QRectF nthButton(const QRectF& first, unsigned int bet)
{
    return first.translated(static_cast<qreal>(bet) * (optWidth + optGap), 0.0);
}

// The bid whose button is under `point`, in a row of 0..highest from `first`.
std::optional<unsigned int> bidUnder(const QRectF& first, unsigned int highest, QPointF point)
{
    for(unsigned int bet = 0 ; bet <= highest ; bet++)
    {
        if(nthButton(first, bet).contains(point))
            return bet;
    }

    return std::nullopt;
}

}

QRectF bidstrip::buttonRect(const Fields& fields, unsigned int bet)
{
    if(bet > fields.highest)
        return QRectF();

    return nthButton(measuredFirstButton(fields), bet);
}

std::optional<unsigned int> bidstrip::bidAt(const Fields& fields, QPointF point)
{
    return bidUnder(measuredFirstButton(fields), fields.highest, point);
}

// ---- the item ----

BidStripItem::BidStripItem(panel::Cache* _panels, QGraphicsItem* parent)
    : QGraphicsObject(parent), panels(_panels ? _panels : &ownPanels)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);

    setPos(tablespace::bidStripOrigin);
    setZValue(tablespace::z::bidStrip);

    // Shown only while the engine is waiting for your bid.
    hide();
}

void BidStripItem::setSnapshot(std::shared_ptr<const GameSnapshot> next, const Theme& palette)
{
    snapshot = std::move(next);

    if(theme.table != palette.table)
    {
        theme = palette;
        memo.invalidate();
        update();
    }

    refresh();
}

void BidStripItem::setPrompt(const BetPrompt& next)
{
    prompt = next;
    pressed.reset();

    refresh();
    show();

    // THE BID UNDER A STILL CURSOR. The scene lights nothing until the mouse moves,
    // and the strip appears wherever the cursor was left - often right over it, in
    // the round dealt from the setup dialog's Deal. The mock-up's browser lights it
    // at once; so does this (phase 7 found the same on the hand).
    //
    // AND THE SCENE PUTS IT OUT (followCursor()): lit this way, the strip is not in
    // the scene's hover list, so no hover leave would ever reach it.
    hoverAt(cursorHere());
}

void BidStripItem::followCursor(std::optional<QPointF> scenePos)
{
    // Nothing lit, nothing to put out - which is every move of the mouse but the
    // few after a strip lights under a resting cursor.
    if(!hovered)
        return;

    // Still on the strip, and then the move is a hover the scene sends it, which
    // puts it in the scene's hover list from here on.
    if(scenePos && QRectF(QPointF(), stripSize).contains(mapFromScene(*scenePos)))
        return;

    hoverAt(std::nullopt);
}

std::optional<QPointF> BidStripItem::cursorHere() const
{
    if(const TableScene* table = qobject_cast<const TableScene*>(scene()))
    {
        if(const std::optional<QPointF> at = table->cursorPosition())
            return mapFromScene(*at);
    }

    return std::nullopt;
}

void BidStripItem::clearPrompt()
{
    prompt.reset();
    pressed.reset();

    // The lit bid goes with the prompt, and the picture with it: the next prompt can
    // show the same bids under no cursor at all.
    if(hovered)
    {
        hovered.reset();
        memo.invalidate();
    }

    hide();
}

bool BidStripItem::activate(unsigned int bet)
{
    if(!prompt || !bidstrip::offers(showing, bet))
        return false;

    emit bidChosen(bet);
    return true;
}

void BidStripItem::refresh()
{
    if(!prompt)
        return;

    // A prompt can arrive before the first snapshot; until one does, the strip
    // reads an empty table.
    static const GameSnapshot none;

    bidstrip::Fields now = bidstrip::fieldsFor(*prompt, snapshot ? *snapshot : none);

    // REDRAWN ONLY WHEN WHAT IT SAYS CHANGES: this runs on every snapshot, and an
    // update() repaints the strip and the table beneath it.
    if(now == showing && !stripSize.isEmpty())
        return;

    // LAID OUT ONCE, here, for the size and the bids' row both: the hit test and
    // the picture read the row from firstBid rather than wrapping the question
    // again (PR #36's review, finding #9).
    const Layout measured = layOut(now, measuring());
    const QSizeF size(tablespace::bidStripWidth, measured.height);

    if(size != stripSize)
    {
        prepareGeometryChange();
        stripSize = size;
    }

    firstBid = firstButton(now, measured.bidsTop);
    showing = std::move(now);

    // New bids are new buttons in new places, so what is lit is read again from
    // where the cursor rests, as the hand does (HandItem::layOut()). Put out
    // instead, a bid under a cursor that had not moved went dark until it did
    // (PR #36's review, finding #3).
    hovered = offeredAt(cursorHere());

    memo.invalidate();
    update();
}

std::optional<unsigned int> BidStripItem::offeredAt(std::optional<QPointF> point) const
{
    if(!prompt || !point)
        return std::nullopt;

    // ONLY AN OFFERED BID LIGHTS. The refused one under the cursor stays as it is:
    // a button that brightens says it can be pressed.
    const std::optional<unsigned int> bet = bidUnder(firstBid, showing.highest, *point);

    if(bet && bidstrip::offers(showing, *bet))
        return bet;

    return std::nullopt;
}

void BidStripItem::hoverAt(std::optional<QPointF> point)
{
    const std::optional<unsigned int> now = offeredAt(point);

    if(now == hovered)
        return;

    hovered = now;
    memo.invalidate();
    update();
}

void BidStripItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    hoverAt(event->pos());
}

void BidStripItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    hoverAt(event->pos());
}

void BidStripItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    hoverAt(std::nullopt);
}

// TAKEN ANYWHERE ON THE PANEL, as SeatItem's are, so a press on the strip - between
// two bids, on its question - never reaches the table beneath it.
void BidStripItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();

    pressed.reset();

    if(const std::optional<unsigned int> bet = bidUnder(firstBid, showing.highest, event->pos());
       prompt && bet && bidstrip::offers(showing, *bet))
    {
        pressed = bet;
    }
}

// A click is a press and a release on the same bid: let go elsewhere, and it is not.
void BidStripItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    const std::optional<unsigned int> was = pressed;
    pressed.reset();

    if(!was || event->button() != Qt::LeftButton)
        return;

    if(bidUnder(firstBid, showing.highest, event->pos()) != was)
        return;

    activate(*was);
}

QRectF BidStripItem::boundingRect() const
{
    if(stripSize.isEmpty())
        return QRectF();

    return QRectF(QPointF(), stripSize).marginsAdded(panel::reach());
}

QPainterPath BidStripItem::shape() const
{
    if(stripSize.isEmpty())
        return QPainterPath();

    return panel::shape(QRectF(QPointF(), stripSize));
}

void BidStripItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(stripSize.isEmpty())
        return;

    const panel::Picture& made = picture(devicepixels::effectiveRatio(*painter));

    if(made.pixmap.isNull())
        return;

    // On a whole device pixel, as the picture's own pixels were laid out to be.
    painter->drawPixmap(devicepixels::snapped(*painter, made.offset), made.pixmap);
}

const panel::Picture& BidStripItem::picture(qreal ratio) const
{
    return memo.picture(ratio, [&]() -> panel::Picture
    {
        const panel::Picture frame = panels->picture(stripSize, theme.table, panel::State::Plain, ratio);

        if(frame.pixmap.isNull())
            return {};

        panel::Picture composed;
        composed.pixmap = QPixmap(frame.pixmap.size());
        composed.pixmap.setDevicePixelRatio(ratio);
        composed.pixmap.fill(Qt::transparent);
        composed.offset = frame.offset;

        QPainter painter(&composed.pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawPixmap(QPointF(0.0, 0.0), frame.pixmap);

        // The strip's own units from here, its top-left at the origin.
        painter.translate(-frame.offset);

        const Theme::TablePalette& palette = theme.table;
        const Styles styles = stylesFor(palette);
        const Layout layout = layOut(showing, styles);

        paintCentred(painter, layout.question, styles.title, layout.questionTop, layout.questionLine);

        // The row the hit test reads, so what is drawn and what takes the cursor
        // are one rectangle.
        const QRectF first = firstBid;

        for(unsigned int bet = 0 ; bet <= showing.highest ; bet++)
        {
            Look look = Look::Plain;

            if(!bidstrip::offers(showing, bet))
                look = Look::Refused;
            else if(hovered == bet)
                look = Look::Lit;

            paintBid(painter, nthButton(first, bet), bet, look, styles, palette);
        }

        paintCentred(painter, layout.note, styles.note, layout.noteTop, layout.noteLine);

        painter.end();

        return composed;
    });
}
