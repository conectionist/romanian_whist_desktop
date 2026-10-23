#include "TrickPanelItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "table/CardText.h"
#include "table/HudText.h"
#include "table/PersonText.h"
#include "table/TrickItem.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include <algorithm>

using romanian_whist::GamePhase;

namespace
{

// THE TWO LINES THAT NAME A SEAT AND AGREE WITH IT in Romanian: "Ana e prima",
// "Bogdan e primul", "Tu ești primul" (persontext::). One English sentence each,
// filed four times for the translator.
constexpr persontext::Sentence leaderSentence{
    "trickpanel",
    { QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about a man"),
      QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about a woman") },
    { QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about you, a man"),
      QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about you, a woman") } };

constexpr persontext::Sentence firstToBidSentence{
    "trickpanel",
    { QT_TRANSLATE_NOOP3("trickpanel", "First to bid: %1", "about a man"),
      QT_TRANSLATE_NOOP3("trickpanel", "First to bid: %1", "about a woman") },
    { QT_TRANSLATE_NOOP3("trickpanel", "First to bid: %1", "about you, a man"),
      QT_TRANSLATE_NOOP3("trickpanel", "First to bid: %1", "about you, a woman") } };

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
    Styles styles;

    styles.title = hudtext::style(fonts::heading(), titleSize, palette.cream, palette);

    // .row: muted, its <b> in cream at weight 500.
    styles.row = hudtext::style(fonts::body(), bodySize, palette.muted, palette);

    QFont medium = fonts::body();
    medium.setWeight(QFont::Medium);

    styles.row.strong = fonts::sized(medium, bodySize).font;
    styles.row.strongColour = palette.cream;

    styles.prompt = hudtext::style(fonts::body(), bodySize, palette.goldBright, palette);

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

// A LINE WITH A NAME IN IT, ON ONE LINE (PR 34's review, finding #2). A name is up
// to 20 characters from the setup dialog, and a line it pushed past the panel's
// width would wrap, making the panel a line taller for as long as it showed - so
// the name gives way instead, elided to what the rest of the line leaves.
//
// `build` makes the line around a name; the name is measured in `style`'s strong
// font if it stands out and its plain one if not. Elided again by however much it
// still runs over, should it: the runs are measured apart, so the line's width is
// not quite the rest's plus the name's.
template<typename Build>
textruns::Line onOneLine(const QString& name, const textruns::Style& style,
                         textruns::Emphasis emphasis, Build&& build)
{
    if(textruns::width(build(name), style) <= contentWidth)
        return build(name);

    const QFontMetricsF metrics(emphasis == textruns::Emphasis::Strong ? style.strong : style.plain);

    qreal room = contentWidth - textruns::width(build(QString()), style);

    for(;;)
    {
        const QString elided = metrics.elidedText(name, Qt::ElideRight, std::max(0.0, room) * style.scale);
        const textruns::Line line = build(elided);
        const qreal over = textruns::width(line, style) - contentWidth;

        if(over <= 0.0 || elided.isEmpty())
            return line;

        room -= over;
    }
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

    return QCoreApplication::translate("trickpanel", "Your turn. No %1, you must trump.").arg(lead);
}

trickpanel::Fields trickpanel::fieldsFor(const GameSnapshot& snapshot, const PlayPrompt* yourPrompt)
{
    // "You" for your own seat, as the mock-up writes every sentence: "You win the
    // trick", "First to bid: You". A spectator has no seat, so is never "You".
    const auto nameOf = [&](unsigned int seat)
    {
        if(snapshot.isHumanSeat(seat))
            return QCoreApplication::translate("trickpanel", "You");

        return seat < snapshot.seats.size() ? QString::fromStdString(snapshot.seats[seat].name)
                                            : QString();
    };

    const bool yoursActive = snapshot.humanSeat >= 0 && snapshot.activeSeat == snapshot.humanSeat;
    const bool anotherActive = snapshot.activeSeat >= 0 && !yoursActive;

    // Every line with a name in it is kept to one line, the name giving way.
    const Styles& styles = measuring();

    const auto thinking = [&]
    {
        return onOneLine(nameOf(static_cast<unsigned int>(snapshot.activeSeat)), styles.prompt,
                         textruns::Emphasis::Plain, [](const QString& name)
                         {
                             return textruns::Line{ textruns::Run{
                                 QCoreApplication::translate("trickpanel", "%1 is thinking…").arg(name) } };
                         });
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
                const unsigned int first = static_cast<unsigned int>(seat);

                fields.rows.push_back(onOneLine(nameOf(first), styles.row, textruns::Emphasis::Strong,
                                                [&snapshot, first](const QString& name)
                                                {
                                                    return textruns::emphasised(
                                                        persontext::translate(firstToBidSentence, snapshot, first), name);
                                                }));
                break;
            }
        }

        if(yoursActive)
            fields.prompt = { textruns::Run{ QCoreApplication::translate("trickpanel", "Your bid. How many tricks?") } };
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
        const auto leaderLine = [&](const QString& name)
        {
            textruns::Line row = textruns::emphasised(persontext::translate(leaderSentence, snapshot, *leader), name);

            if(snapshot.hasLeadSuit)
            {
                row.push_back(textruns::Run{ QStringLiteral(" · ")
                                             + QCoreApplication::translate("trickpanel", "Led %1").arg(cardtext::symbol(snapshot.leadSuit)) });
            }

            return row;
        };

        fields.rows.push_back(onOneLine(nameOf(*leader), styles.row, textruns::Emphasis::Strong, leaderLine));
    }

    // WHO IS WINNING: the engine's ranking, never recomputed, from the first card
    // on - as the mock-up writes it, though the felt rings the winner only from the
    // second (section 2.7).
    const auto winning = std::find_if(snapshot.table.begin(), snapshot.table.end(),
                                      [](const PlayedCardView& played) { return played.isWinning; });

    const auto winningLine = [](const QString& who)
    {
        return textruns::emphasised(QCoreApplication::translate("trickpanel", "Winning: %1"), who);
    };

    if(winning == snapshot.table.end())
    {
        fields.rows.push_back(winningLine(hudtext::dash()));
    }
    else
    {
        const QString card = cardtext::label(winning->card);

        fields.rows.push_back(onOneLine(nameOf(winning->seatIndex), styles.row, textruns::Emphasis::Strong,
                                        [&](const QString& name)
                                        {
                                            return winningLine(name + QStringLiteral(", ") + card);
                                        }));
    }

    const bool trickFull = !snapshot.seats.empty() && snapshot.table.size() >= snapshot.seats.size();

    if(trickFull && winning != snapshot.table.end())
    {
        const bool yours = snapshot.isHumanSeat(winning->seatIndex);

        if(yours)
        {
            fields.prompt = { textruns::Run{ QCoreApplication::translate("trickpanel", "You win the trick.") } };
        }
        else
        {
            fields.prompt = onOneLine(nameOf(winning->seatIndex), styles.prompt, textruns::Emphasis::Plain,
                                      [](const QString& name)
                                      {
                                          return textruns::Line{ textruns::Run{
                                              QCoreApplication::translate("trickpanel", "%1 wins the trick.").arg(name) } };
                                      });
        }
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

void TrickPanelItem::setSnapshot(std::shared_ptr<const GameSnapshot> next, const Theme& palette)
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
    // A prompt can arrive before the first snapshot; until one does, the panel
    // reads an empty table, as it did when it held one by value.
    static const GameSnapshot none;

    trickpanel::Fields now = trickpanel::fieldsFor(snapshot ? *snapshot : none, prompt ? &*prompt : nullptr);

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
