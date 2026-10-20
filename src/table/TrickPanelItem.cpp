#include "TrickPanelItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "table/CardText.h"
#include "table/TrickItem.h"

#include <QCoreApplication>
#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include <algorithm>

using romanian_whist::GamePhase;

namespace
{

// page.html's .trickinfo, in the panel's own units: a border of 1, padding of 12
// above and below and 16 either side, and its text 17 on lines 1.45 tall.
constexpr qreal borderWidth = 1.0;
constexpr qreal paddingX = 16.0;
constexpr qreal paddingY = 12.0;
constexpr qreal bodySize = 17.0;
constexpr qreal lineFactor = 1.45;

// .title: Marcellus 21, on the panel's line height, 2 above the rows.
constexpr qreal titleSize = 21.0;
constexpr qreal titleGap = 2.0;

// .prompt: 6 above a 1-unit rule, and 6 between the rule and the words.
constexpr qreal promptGap = 6.0;
constexpr qreal ruleWidth = 1.0;

constexpr qreal contentWidth = trickpanel::width - 2.0 * (borderWidth + paddingX);

struct Styles
{
    textruns::Style title;
    textruns::Style row;
    textruns::Style prompt;
};

Styles stylesFor(const Theme::TablePalette& palette)
{
    const auto make = [&](const QFont& font, qreal pixels, const QColor& colour)
    {
        textruns::Style style = textruns::plainStyle(font, pixels, colour);
        style.redSuit = palette.redSuit;
        style.blackSuit = palette.blackSuit;
        return style;
    };

    Styles styles;

    styles.title = make(fonts::heading(), titleSize, palette.cream);

    // .row: muted, its <b> in cream at weight 500.
    styles.row = make(fonts::body(), bodySize, palette.muted);

    QFont medium = fonts::body();
    medium.setWeight(QFont::Medium);

    styles.row.strong = fonts::sized(medium, bodySize).font;
    styles.row.strongColour = palette.cream;

    styles.prompt = make(fonts::body(), bodySize, palette.goldBright);

    return styles;
}

// The panel laid out from its top-left, painted as it goes if there is a painter:
// the one walk that both sizes the panel and draws it, as the scoreboard's is.
qreal layOut(const trickpanel::Fields& fields, const Styles& styles, QPainter* painter,
             const Theme::TablePalette* palette)
{
    const qreal left = borderWidth + paddingX;
    const qreal line = bodySize * lineFactor;

    const auto lines = [&](const textruns::Line& text, const textruns::Style& style, qreal& y)
    {
        // An empty line still takes its line: the prompt keeps its place while
        // nobody is being asked anything, so the panel does not jump.
        std::vector<textruns::Line> wrapped = textruns::wrap(text, style, contentWidth);

        if(wrapped.empty())
            wrapped.emplace_back();

        for(const textruns::Line& one : wrapped)
        {
            if(painter)
                textruns::paint(*painter, QPointF(left, textruns::baselineIn(style, y, line)), one, style);

            y += line;
        }
    };

    qreal y = borderWidth + paddingY;

    const qreal titleLine = titleSize * lineFactor;

    if(painter)
    {
        textruns::paint(*painter, QPointF(left, textruns::baselineIn(styles.title, y, titleLine)),
                        { textruns::Run{ fields.title } }, styles.title);
    }

    y += titleLine + titleGap;

    for(const textruns::Line& row : fields.rows)
        lines(row, styles.row, y);

    y += promptGap;

    if(painter)
        painter->fillRect(QRectF(left, y, contentWidth, ruleWidth), palette->hudRule);

    y += ruleWidth + promptGap;

    lines(fields.prompt, styles.prompt, y);

    return y + paddingY + borderWidth;
}

const Styles& measuring()
{
    static const Styles styles = stylesFor(Theme::dark().table);
    return styles;
}

}

QString trickpanel::yourTurn(const PlayPrompt& prompt, const GameSnapshot& snapshot)
{
    // A BLIND ROUND HAS NOTHING TO EXPLAIN. There is one card, it is face down,
    // and it is forced - so "you must follow" would describe a choice the player
    // does not have.
    if(prompt.blind)
        return QCoreApplication::translate("trickpanel", "Your turn.");

    // Nothing played yet, so there is no suit to follow and every card is legal.
    if(!snapshot.hasLeadSuit)
        return QCoreApplication::translate("trickpanel", "Your turn. You lead.");

    // RESTRICTED MEANS AN ILLEGAL CARD IN THE HAND, asked of the entries rather
    // than inferred from the lead suit. A player void in the lead suit with no
    // trump may play anything, so there is a lead suit and nothing to follow.
    const bool restricted = std::any_of(prompt.entries.begin(), prompt.entries.end(),
                                        [](const HandEntry& entry){ return !entry.legal; });

    if(!restricted)
        return QCoreApplication::translate("trickpanel", "Your turn. Any card.");

    const QString lead = cardtext::symbol(snapshot.leadSuit);

    // Restricted to the lead suit, or - void in it - to the trumps, which RULES.md
    // makes a player play when they cannot follow. The legal cards say which.
    const bool following = std::any_of(prompt.entries.begin(), prompt.entries.end(),
                                       [&](const HandEntry& entry)
                                       {
                                           return entry.legal && entry.card.suit == snapshot.leadSuit;
                                       });

    if(following)
        return QCoreApplication::translate("trickpanel", "Your turn. You must follow %1.").arg(lead);

    return QCoreApplication::translate("trickpanel", "Your turn. No %1, so you must trump.").arg(lead);
}

trickpanel::Fields trickpanel::fieldsFor(const GameSnapshot& snapshot, const PlayPrompt* yourPrompt)
{
    // "You" for your own seat, as the mock-up writes every sentence: "You win the
    // trick", "First to bid: You". A spectator has no seat, so is never "You".
    const auto nameOf = [&](unsigned int seat)
    {
        if(snapshot.humanSeat >= 0 && static_cast<unsigned int>(snapshot.humanSeat) == seat)
            return QCoreApplication::translate("trickpanel", "You");

        return seat < snapshot.seats.size() ? QString::fromStdString(snapshot.seats[seat].name)
                                            : QString();
    };

    const bool yoursActive = snapshot.humanSeat >= 0 && snapshot.activeSeat == snapshot.humanSeat;
    const bool anotherActive = snapshot.activeSeat >= 0 && !yoursActive;

    const auto thinking = [&]
    {
        return textruns::Line{ textruns::Run{ QCoreApplication::translate("trickpanel", "%1 is thinking…")
                                                  .arg(nameOf(static_cast<unsigned int>(snapshot.activeSeat))) } };
    };

    Fields fields;

    if(snapshot.phase == GamePhase::RoundScored)
    {
        fields.title = QCoreApplication::translate("trickpanel", "Round over");
        fields.prompt = { textruns::Run{ QCoreApplication::translate("trickpanel", "Scores are in.") } };
        return fields;
    }

    if(snapshot.phase == GamePhase::GameOver)
    {
        fields.title = QCoreApplication::translate("trickpanel", "Game over");
        fields.prompt = { textruns::Run{ QCoreApplication::translate("trickpanel", "Final scores are in.") } };
        return fields;
    }

    if(snapshot.phase != GamePhase::Playing)
    {
        fields.title = QCoreApplication::translate("trickpanel", "Bidding");

        for(std::size_t seat = 0 ; seat < snapshot.seats.size() ; seat++)
        {
            if(snapshot.seats[seat].bidOrder == 1)
            {
                fields.rows.push_back(textruns::emphasised(QCoreApplication::translate("trickpanel", "First to bid: %1"),
                                                           nameOf(static_cast<unsigned int>(seat))));
                break;
            }
        }

        if(yoursActive)
            fields.prompt = { textruns::Run{ QCoreApplication::translate("trickpanel", "Your bid. How many tricks will you take?") } };
        else if(anotherActive)
            fields.prompt = thinking();

        return fields;
    }

    // THE PLAY. Trick 0 is "dealt and bid, nothing played yet", which the engine
    // reports between the last bid and the first trick: that trick is the first.
    const unsigned int trick = std::clamp(snapshot.trickNumber, 1u, std::max(snapshot.trickCount, 1u));

    fields.title = QCoreApplication::translate("trickpanel", "Trick %1 of %2").arg(QString::number(trick),
                                                    QString::number(snapshot.trickCount));

    // WHO LEADS, by the rhombus's own rule (trickitem::leadSeat()): the first card's
    // owner once it is down, the engine's leader before.
    if(const std::optional<unsigned int> leader = trickitem::leadSeat(snapshot))
    {
        textruns::Line row = textruns::emphasised(QCoreApplication::translate("trickpanel", "Leader: %1"), nameOf(*leader));

        if(snapshot.hasLeadSuit)
        {
            row.push_back(textruns::Run{ QStringLiteral(" · ")
                                         + QCoreApplication::translate("trickpanel", "Led %1").arg(cardtext::symbol(snapshot.leadSuit)) });
        }

        fields.rows.push_back(row);
    }

    // WHO IS WINNING: the engine's ranking, never recomputed, from the first card
    // on - as the mock-up writes it, though the felt rings the winner only from the
    // second (section 2.7).
    const auto winning = std::find_if(snapshot.table.begin(), snapshot.table.end(),
                                      [](const PlayedCardView& played) { return played.isWinning; });

    fields.rows.push_back(textruns::emphasised(
        QCoreApplication::translate("trickpanel", "Winning: %1"),
        winning == snapshot.table.end()
            ? QStringLiteral("–")
            : nameOf(winning->seatIndex) + QStringLiteral(", ") + cardtext::label(winning->card)));

    const bool trickFull = !snapshot.seats.empty() && snapshot.table.size() >= snapshot.seats.size();

    if(trickFull && winning != snapshot.table.end())
    {
        const bool yours = snapshot.humanSeat >= 0
                           && static_cast<unsigned int>(snapshot.humanSeat) == winning->seatIndex;

        fields.prompt = { textruns::Run{ yours ? QCoreApplication::translate("trickpanel", "You win the trick.")
                                               : QCoreApplication::translate("trickpanel", "%1 wins the trick.").arg(nameOf(winning->seatIndex)) } };
    }
    else if(yourPrompt)
    {
        fields.prompt = { textruns::Run{ yourTurn(*yourPrompt, snapshot) } };
    }
    else if(yoursActive)
    {
        // Asked, and the prompt not here yet: it arrives a moment after the frame
        // that names your seat.
        fields.prompt = { textruns::Run{ QCoreApplication::translate("trickpanel", "Your turn.") } };
    }
    else if(anotherActive)
    {
        fields.prompt = thinking();
    }

    return fields;
}

QSizeF trickpanel::sizeFor(const Fields& fields)
{
    return QSizeF(width, layOut(fields, measuring(), nullptr, nullptr));
}

TrickPanelItem::TrickPanelItem(panel::Cache* _panels, QGraphicsItem* parent)
    : QGraphicsObject(parent), panels(_panels ? _panels : &ownPanels)
{
}

void TrickPanelItem::setSnapshot(const GameSnapshot& next, const Theme& palette)
{
    snapshot = next;

    if(theme.table != palette.table)
    {
        theme = palette;
        memo.invalidate();
        update();
    }

    refresh();
}

void TrickPanelItem::setPrompt(const PlayPrompt& next)
{
    prompt = next;
    refresh();
}

void TrickPanelItem::clearPrompt()
{
    prompt.reset();
    refresh();
}

void TrickPanelItem::refresh()
{
    trickpanel::Fields now = trickpanel::fieldsFor(snapshot, prompt ? &*prompt : nullptr);

    // REDRAWN ONLY WHEN WHAT IT SAYS CHANGES: this runs on every snapshot, and an
    // update() repaints the panel and what lies beneath it.
    if(now == showing && !panelSize.isEmpty())
        return;

    const QSizeF size = trickpanel::sizeFor(now);

    if(size != panelSize)
    {
        prepareGeometryChange();
        panelSize = size;
    }

    showing = std::move(now);
    memo.invalidate();
    update();
}

QRectF TrickPanelItem::boundingRect() const
{
    if(panelSize.isEmpty())
        return QRectF();

    return QRectF(QPointF(), panelSize).marginsAdded(panel::reach());
}

QPainterPath TrickPanelItem::shape() const
{
    if(panelSize.isEmpty())
        return QPainterPath();

    return panel::shape(QRectF(QPointF(), panelSize));
}

void TrickPanelItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

void TrickPanelItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(panelSize.isEmpty())
        return;

    const panel::Picture& made = picture(devicepixels::effectiveRatio(*painter));

    if(made.pixmap.isNull())
        return;

    painter->drawPixmap(devicepixels::snapped(*painter, made.offset), made.pixmap);
}

const panel::Picture& TrickPanelItem::picture(qreal ratio) const
{
    return memo.picture(ratio, [&]() -> panel::Picture
    {
        const panel::Picture frame = panels->picture(panelSize, theme.table, panel::State::Plain, ratio);

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

        // The panel's own units from here, its top-left at the origin.
        painter.translate(-frame.offset);

        layOut(showing, stylesFor(theme.table), &painter, &theme.table);
        painter.end();

        return composed;
    });
}
