#include "ScoreboardItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"
#include "table/CardText.h"
#include "table/HudText.h"
#include "table/RoundText.h"
#include "table/TableSpace.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include <algorithm>
#include <numeric>

namespace
{

// page.html's .score, in the panel's own units: a border of 1, padding of 12 above
// and below and 14 either side.
constexpr qreal borderWidth = 1.0;
constexpr qreal paddingX = 14.0;
constexpr qreal paddingY = 12.0;

// The widest the content grows: the mock-up's 236, less its border and padding.
constexpr qreal widestContent = scoreboard::mockupWidth - 2.0 * (borderWidth + paddingX);

// .round: Marcellus 22. .sub: 15, 1 under it.
constexpr qreal roundSize = 22.0;
constexpr qreal subSize = 15.0;
constexpr qreal subGap = 1.0;

// .tension: 14.5 on lines 1.3 tall, 8 under the .sub.
constexpr qreal tensionSize = 14.5;
constexpr qreal tensionGap = 8.0;
constexpr qreal tensionLine = 1.3;

// The table, 8 under the tension. th: 12, bold, spaced 0.1em, padded 4 above and
// below, a 1-unit rule under it. td: 17, padded 3.
constexpr qreal tableGap = 8.0;
constexpr qreal headSize = 12.0;
constexpr qreal headSpacing = 0.1;
constexpr qreal headPadding = 4.0;
constexpr qreal ruleWidth = 1.0;
constexpr qreal cellSize = 17.0;
constexpr qreal cellPadding = 3.0;

// THE ROOM BEFORE EACH NUMBER COLUMN. The mock-up has none to copy: its table is
// the panel's fixed width, and the browser shares out whatever its columns leave.
// The panel is as wide as its columns instead (the owner's call, 2026-10-20), so
// the room between them is set here.
constexpr qreal columnGap = 12.0;

// .divider: 1 tall, 8 either side.
constexpr qreal dividerGap = 8.0;

// .trump: a column with 8 between its items. Its .label 19.8, bold, spaced 0.24em;
// its .name Marcellus 19.8 on a line 1.1 tall. The card rounded 10, its shadow
// 0 4px 12px.
constexpr qreal trumpGap = 8.0;
constexpr qreal trumpLabelSize = 19.8;
constexpr qreal trumpLabelSpacing = 0.24;
constexpr qreal trumpNameSize = 19.8;
constexpr qreal trumpNameLine = 1.1;
constexpr qreal trumpRadius = 10.0;
constexpr QPointF trumpShadowOffset(0.0, 4.0);
constexpr qreal trumpShadowBlur = 12.0;

// The turn's mark before a name, as .score tr.turn td:first-child::before writes it:
// content: "\25B8 ", whose space is not a space - CSS takes the one whitespace
// after a hex escape as the escape's end - so the mark sits against the name.
const QString turnMarker = QStringLiteral("▸");

// One style for every piece of the panel's text.
struct Styles
{
    textruns::Style round;
    textruns::Style sub;
    textruns::Style tension;
    textruns::Style head;
    textruns::Style cell;
    textruns::Style trumpLabel;
    textruns::Style trumpName;
};

// The two fonts spaced by `em` of their size and set in capitals, as .label and
// th are (fonts::capitals()).
void capitalsSpaced(textruns::Style& style, qreal em)
{
    for(QFont* font : { &style.plain, &style.strong })
        *font = fonts::capitals(*font, em);
}

Styles stylesFor(const Theme::TablePalette& palette)
{
    QFont bold = fonts::body();
    bold.setWeight(QFont::Bold);

    Styles styles;

    styles.round = hudtext::style(fonts::heading(), roundSize, palette.cream, palette);
    styles.sub = hudtext::style(fonts::body(), subSize, palette.muted, palette);

    // .tension b: the number in bold cream, in a muted line.
    styles.tension = hudtext::style(fonts::body(), tensionSize, palette.muted, palette);
    styles.tension.strong = fonts::sized(bold, tensionSize).font;
    styles.tension.strongColour = palette.cream;

    styles.head = hudtext::style(bold, headSize, palette.muted, palette);
    capitalsSpaced(styles.head, headSpacing);

    // TABULAR FIGURES (section 18), so a 1 and a 0 occupy the same box and a
    // column of scores lines up on its own.
    const QFont cell = fonts::tabular(fonts::body());

    styles.cell = hudtext::style(cell, cellSize, palette.cream, palette);

    styles.trumpLabel = hudtext::style(bold, trumpLabelSize, palette.muted, palette);
    capitalsSpaced(styles.trumpLabel, trumpLabelSpacing);

    styles.trumpName = hudtext::style(fonts::heading(), trumpNameSize, palette.cream, palette);

    return styles;
}

// The styles the panel is MEASURED in. The colours do not change a width, so any
// palette's would do; the dark one is the table's.
const Styles& measuring()
{
    static const Styles styles = stylesFor(Theme::dark().table);
    return styles;
}

qreal widthOf(const QString& text, const textruns::Style& style)
{
    return textruns::width({ textruns::Run{ text } }, style);
}

// The trump card's size in the panel's units: a hand card's, which is what
// section 2.9 asks for (154.9 table units wide), drawn inside a panel already
// scaled by tablespace::hudScale.
QSizeF trumpCardSize(const CardRenderer& cards)
{
    return QSizeF(cards.sizeForHeight(tablespace::handCardHeight)) / tablespace::hudScale;
}

// A painter, or nothing: the panel is laid out by the same walk that paints it, so
// the size it asks for is the size it draws: the old scoreboard widget once worked
// its columns out twice, and the two drifted.
struct Canvas
{
    QPainter* painter = nullptr;
    const Theme::TablePalette* palette = nullptr;
    const CardRenderer* cards = nullptr;
    qreal ratio = 1.0;
};

// Lays the panel out from its top-left, painting as it goes if there is a painter,
// and returns its height. `columns` are columnsFor()'s for the rows, worked out
// once for a change and handed to both walks, the one that sizes the panel and
// the one that paints it: each measures every name (PR 34's review, finding #13).
qreal layOut(const scoreboard::Shown& shown, const scoreboard::Columns& columns, const Styles& styles,
             const Canvas& canvas)
{
    QPainter* painter = canvas.painter;

    const qreal left = borderWidth + paddingX;
    const qreal width = columns.sum();

    // One line, centred in the content.
    const auto centred = [&](const textruns::Line& line, const textruns::Style& style,
                             qreal top, qreal height, qreal trailing = 0.0)
    {
        if(!painter)
            return;

        const qreal drawn = textruns::width(line, style) - trailing;

        textruns::paint(*painter, QPointF(left + (width - drawn) / 2.0,
                                          textruns::baselineIn(style, top, height)),
                        line, style);
    };

    qreal y = borderWidth + paddingY;

    // "Round 11 of 30".
    const qreal roundHeight = textruns::metrics(styles.round).lineSpacing;
    centred({ textruns::Run{ shown.header.round } }, styles.round, y, roundHeight);
    y += roundHeight;

    // "6 cards dealt · Normal", the round type gold-bright when it is called out.
    y += subGap;

    textruns::Style sub = styles.sub;

    if(canvas.palette && shown.header.calledOut)
        sub.strongColour = canvas.palette->goldBright;

    const qreal subHeight = textruns::metrics(sub).lineSpacing;
    centred({ textruns::Run{ shown.header.dealt + QStringLiteral(" · ") },
              textruns::Run{ shown.header.roundType, textruns::Emphasis::Strong } },
            sub, y, subHeight);
    y += subHeight;

    // The tension, wrapped if it has to be, each line centred.
    y += tensionGap;

    const qreal tensionHeight = tensionSize * tensionLine;

    for(const textruns::Line& line : textruns::wrap(shown.tension, styles.tension, width))
    {
        centred(line, styles.tension, y, tensionHeight);
        y += tensionHeight;
    }

    // THE TABLE. Every column but the names right-aligned, each at its own right
    // edge, worked out once for the headings and the rows alike.
    y += tableGap;

    const qreal playerRight = left + columns.player;
    const qreal bidRight = playerRight + columns.bid;
    const qreal takenRight = bidRight + columns.taken;
    const qreal pointsRight = takenRight + columns.points;
    const qreal runRight = pointsRight + columns.run;

    const auto rightAligned = [&](const QString& text, const textruns::Style& style, qreal right,
                                  qreal baseline)
    {
        if(painter && !text.isEmpty())
        {
            const textruns::Line line{ textruns::Run{ text } };
            textruns::paint(*painter, QPointF(right - textruns::width(line, style), baseline), line, style);
        }
    };

    const qreal headLine = textruns::metrics(styles.head).lineSpacing;
    const qreal headHeight = 2.0 * headPadding + headLine + ruleWidth;

    if(painter)
    {
        const qreal baseline = textruns::baselineIn(styles.head, y + headPadding, headLine);

        textruns::paint(*painter, QPointF(left, baseline),
                        { textruns::Run{ scoreboard::headingPlayer() } }, styles.head);

        rightAligned(scoreboard::headingBid(), styles.head, bidRight, baseline);
        rightAligned(scoreboard::headingTaken(), styles.head, takenRight, baseline);
        rightAligned(scoreboard::headingPoints(), styles.head, pointsRight, baseline);
        rightAligned(scoreboard::headingRun(), styles.head, runRight, baseline);

        painter->fillRect(QRectF(left, y + headHeight - ruleWidth, width, ruleWidth),
                          canvas.palette->hudRule);
    }

    y += headHeight;

    const qreal cellLine = textruns::metrics(styles.cell).lineSpacing;
    const qreal rowHeight = 2.0 * cellPadding + cellLine;

    for(const scoreboard::Row& row : shown.rows)
    {
        if(painter)
        {
            const Theme::TablePalette& palette = *canvas.palette;
            const qreal baseline = textruns::baselineIn(styles.cell, y + cellPadding, cellLine);

            // The row being asked to move, gold-bright, its name after a "▸".
            textruns::Style cell = styles.cell;
            cell.plainColour = cell.strongColour = row.turn ? palette.goldBright : palette.cream;

            qreal x = left;

            if(row.turn)
            {
                const textruns::Line mark{ textruns::Run{ turnMarker } };
                textruns::paint(*painter, QPointF(x, baseline), mark, cell);
                x += textruns::width(mark, cell);
            }

            // Elided, because a name is arbitrary text from the setup dialog and a
            // column is a fixed width. The width is the turn's too, so a long name
            // does not change length as the turn reaches it.
            const qreal room = columns.player - widthOf(turnMarker, cell);
            const QFontMetricsF names(cell.plain);
            const QString name = names.elidedText(row.name, Qt::ElideRight, room * cell.scale);

            textruns::paint(*painter, QPointF(x, baseline), { textruns::Run{ name } }, cell);

            rightAligned(row.bid, cell, bidRight, baseline);
            rightAligned(row.taken, cell, takenRight, baseline);
            rightAligned(row.points, cell, pointsRight, baseline);

            // The run in its own colour, on the turn's row as on any other: the
            // colour means something here, which the turn's gold would hide.
            textruns::Style run = styles.cell;
            run.plainColour = row.streak == scoreboard::Streak::Losing ? palette.bad : palette.good;

            rightAligned(row.run, run, runRight, baseline);
        }

        y += rowHeight;
    }

    // The divider, then the trump stacked and centred: "TRUMP", the card, its suit.
    y += dividerGap;

    if(painter)
        painter->fillRect(QRectF(left, y, width, ruleWidth), canvas.palette->hudRule);

    y += ruleWidth + dividerGap;

    // CENTRED WITHOUT THE SPACING AFTER ITS LAST LETTER, which is what the
    // mock-up's margin-right: -0.24em takes back.
    const QString label = QCoreApplication::translate("scoreboard", "Trump");
    const qreal labelHeight = textruns::metrics(styles.trumpLabel).lineSpacing;

    centred({ textruns::Run{ label } }, styles.trumpLabel, y, labelHeight,
            trumpLabelSpacing * trumpLabelSize);
    y += labelHeight + trumpGap;

    if(shown.trump.hasCard && canvas.cards)
    {
        const QSizeF size = trumpCardSize(*canvas.cards);

        if(painter)
        {
            // On a whole pixel of the picture, so the card - asked for at exactly
            // the pixels it covers - is blitted 1:1.
            const QRectF card(devicepixels::snapped(*painter,
                                                    QPointF(left + (width - size.width()) / 2.0, y)),
                              size);

            // Beneath, not cut out: the card is opaque and laid straight over it,
            // as a rack's cards are (PHASE5_REVIEW_FINDINGS.md #1).
            shadows::drawShapeShadow(*painter, paths::rounded(card, trumpRadius), trumpShadowOffset,
                                     trumpShadowBlur, canvas.palette->trumpShadow,
                                     shadows::Side::Beneath);

            // AT A HAND CARD'S SIZE AND RATIO, which is the same pixels - so the
            // trump shares its picture with the same card in your hand.
            const QPixmap face = canvas.cards->face(shown.trump.card,
                                                    canvas.cards->sizeForHeight(tablespace::handCardHeight),
                                                    canvas.ratio / tablespace::hudScale);

            painter->drawPixmap(card, face, QRectF(face.rect()));
        }

        y += size.height() + trumpGap;
    }

    const qreal nameHeight = trumpNameSize * trumpNameLine;
    centred({ textruns::Run{ shown.trump.name } }, styles.trumpName, y, nameHeight);
    y += nameHeight;

    return y + paddingY + borderWidth;
}

}

// ---- the scoreboard ----

QString scoreboard::dealtText(unsigned int trickCount)
{
    // Two strings, for cardsText()'s reason.
    return trickCount == 1 ? QCoreApplication::translate("scoreboard", "1 card dealt")
                           : QCoreApplication::translate("scoreboard", "%1 cards dealt")
                                 .arg(trickCount);
}

scoreboard::Header scoreboard::headerFor(const GameSnapshot& snapshot)
{
    const infostrip::Fields fields = infostrip::fieldsFor(snapshot);

    return { fields.round, dealtText(snapshot.trickCount), fields.roundType,
             infostrip::isCalledOut(snapshot.roundType) };
}

namespace
{

// The tension's four sentences: while the bids come in, then over, under or even.
enum class Tension { SoFar, Over, Under, Even };

// One of them with its numbers in, the bid total bold. `difference` is the "by" of
// Over and Under, and unused by the others; `oneTrick` picks the singular.
//
// THE NUMBERS AS TEXT, so widestTension() can write them in the font's widest
// digit - which is not a number it could pass.
textruns::Line tensionText(Tension form, const QString& bids, const QString& tricks,
                           const QString& difference, bool oneTrick)
{
    QString sentence;

    // Singular or plural on the trick count, for cardsText()'s reason.
    switch(form)
    {
    case Tension::SoFar:
        sentence = oneTrick ? QCoreApplication::translate("scoreboard", "Bids so far: %1 of %2 trick")
                            : QCoreApplication::translate("scoreboard", "Bids so far: %1 of %2 tricks");
        break;

    case Tension::Over:
        sentence = oneTrick ? QCoreApplication::translate("scoreboard", "Bids total %1 for %2 trick: over by %3")
                            : QCoreApplication::translate("scoreboard", "Bids total %1 for %2 tricks: over by %3");
        break;

    case Tension::Under:
        sentence = oneTrick ? QCoreApplication::translate("scoreboard", "Bids total %1 for %2 trick: under by %3")
                            : QCoreApplication::translate("scoreboard", "Bids total %1 for %2 tricks: under by %3");
        break;

    case Tension::Even:
        sentence = oneTrick ? QCoreApplication::translate("scoreboard", "Bids total %1 for %2 trick")
                            : QCoreApplication::translate("scoreboard", "Bids total %1 for %2 tricks");
        break;
    }

    const bool by = form == Tension::Over || form == Tension::Under;

    // THE BID TOTAL IS THE BOLD PART, so it is cut out of the sentence rather than
    // substituted into it: %1 is swapped for a character no translation contains,
    // the rest filled in by one arg() - whichever side of %1 a translation puts
    // them - and the sentence split where that character is.
    const QChar cut(0xE000);

    sentence.replace(QStringLiteral("%1"), QString(cut));

    // %2 and %3, one pass: arg() fills the lowest marker still standing first. A
    // sentence without %3 leaves the second argument unused, and arg() asks for
    // none.
    sentence = by ? sentence.arg(tricks, difference) : sentence.arg(tricks);

    const qsizetype at = sentence.indexOf(cut);

    if(at < 0)
        return { textruns::Run{ sentence } };

    textruns::Line line;

    if(at > 0)
        line.push_back({ sentence.left(at) });

    line.push_back({ bids, textruns::Emphasis::Strong });

    if(at + 1 < sentence.size())
        line.push_back({ sentence.mid(at + 1) });

    return line;
}

// The widest of the ten digits in `font`.
QChar widestDigit(const QFont& font)
{
    const QFontMetricsF metrics(font);
    QChar widest(u'0');

    for(char16_t digit = u'1' ; digit <= u'9' ; digit++)
    {
        if(metrics.horizontalAdvance(QChar(digit)) > metrics.horizontalAdvance(widest))
            widest = QChar(digit);
    }

    return widest;
}

// THE TENSION AT ITS WIDEST, which the content is never narrower than: so the line
// never wraps, and the panel changes neither its height as the bids come in nor
// its width as they change. Its numbers as long as a game can make them - up to six
// seats bidding up to eight each, so a total of two digits and over by as many; at
// most eight tricks, so one digit for the tricks, and for a total under them and
// the "by" - and every digit the font's widest.
//
// THE WIDEST DIGIT, NOT AN 8. The tension's figures are proportional, and in
// Alegreya Sans a 0 and a 4 are wider than an 8: measured with 8s, the widest
// sentence fell a unit short of "Bids total 24 for 4 tricks: over by 20", which
// wrapped (PR 34's review, after its fixes). The bid total is bold, so its digit is
// the strong font's.
qreal widestTension(const textruns::Style& style)
{
    const QString one(1, widestDigit(style.plain));
    const QString two(2, widestDigit(style.plain));
    const QString boldOne(1, widestDigit(style.strong));
    const QString boldTwo(2, widestDigit(style.strong));

    return std::max({ textruns::width(tensionText(Tension::SoFar, boldTwo, one, QString(), false), style),
                      textruns::width(tensionText(Tension::Over, boldTwo, one, two, false), style),
                      textruns::width(tensionText(Tension::Under, boldOne, one, one, false), style),
                      textruns::width(tensionText(Tension::Even, boldOne, one, QString(), false), style) });
}

}

textruns::Line scoreboard::tensionFor(const GameSnapshot& snapshot)
{
    const unsigned int tricks = snapshot.trickCount;
    const unsigned int bids = snapshot.betSum;

    // Over or under once every bid is in. The forbidden bid means the total never
    // equals the tricks once the last bid is made, but the sentence says nothing
    // wrong if it ever does.
    const auto text = [tricks](Tension form, unsigned int total, unsigned int difference)
    {
        return tensionText(form, QString::number(total), QString::number(tricks),
                           QString::number(difference), tricks == 1);
    };

    if(snapshot.betsPlaced < snapshot.seats.size())
        return text(Tension::SoFar, bids, 0);

    if(bids > tricks)
        return text(Tension::Over, bids, bids - tricks);

    if(bids < tricks)
        return text(Tension::Under, bids, tricks - bids);

    return text(Tension::Even, bids, 0);
}

std::vector<scoreboard::Row> scoreboard::rowsFor(const GameSnapshot& snapshot)
{
    // Nobody has taken a trick while the round is being bid.
    const bool bidding = snapshot.phase == romanian_whist::GamePhase::Betting
                         || snapshot.phase == romanian_whist::GamePhase::NotStarted;

    std::vector<std::size_t> order(snapshot.seats.size());
    std::iota(order.begin(), order.end(), std::size_t(0));

    // BID ORDER, as the mock-up's rows: an order of 0 is "not known yet", and
    // sorts after the known ones in seat order rather than claiming the top row.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
    {
        const unsigned int first = snapshot.seats[a].bidOrder;
        const unsigned int second = snapshot.seats[b].bidOrder;

        if(first == 0 || second == 0)
            return first != 0 && second == 0;

        return first < second;
    });

    std::vector<Row> rows;
    rows.reserve(order.size());

    for(const std::size_t index : order)
    {
        const SeatView& seat = snapshot.seats[index];

        Row row;

        row.name = QString::fromStdString(seat.name);
        row.bid = seat.hasBet ? QString::number(seat.bet) : hudtext::dash();
        row.taken = bidding ? hudtext::dash() : QString::number(seat.tricksWon);
        row.points = QString::number(seat.totalScore);

        // A win streak and a loss streak are never both running.
        if(seat.winStreak > 0)
        {
            row.run = QCoreApplication::translate("scoreboard", "W%1").arg(seat.winStreak);
            row.streak = Streak::Winning;
        }
        else if(seat.lossStreak > 0)
        {
            row.run = QCoreApplication::translate("scoreboard", "L%1").arg(seat.lossStreak);
            row.streak = Streak::Losing;
        }

        row.turn = snapshot.activeSeat >= 0 && static_cast<std::size_t>(snapshot.activeSeat) == index;

        rows.push_back(row);
    }

    return rows;
}

QString scoreboard::headingPlayer()
{
    return QCoreApplication::translate("scoreboard", "Player");
}

// ONE LETTER EACH, so the panel is no wider than its numbers need (the owner's
// call, 2026-10-20).
QString scoreboard::headingBid()
{
    return QCoreApplication::translate("scoreboard", "B");
}

QString scoreboard::headingTaken()
{
    return QCoreApplication::translate("scoreboard", "T");
}

QString scoreboard::headingPoints()
{
    return QCoreApplication::translate("scoreboard", "Pts");
}

QString scoreboard::headingRun()
{
    return QCoreApplication::translate("scoreboard", "Run");
}

scoreboard::Trump scoreboard::trumpFor(const GameSnapshot& snapshot)
{
    // Named rather than left blank. The eight-card round having no trump is a rule
    // of the game; an empty space says the client forgot.
    if(!snapshot.hasTrump)
        return { false, romanian_whist::Card{}, QCoreApplication::translate("scoreboard", "None this round") };

    return { true, snapshot.trump,
             cardtext::symbol(snapshot.trump.suit) + QLatin1Char(' ')
                 + cardtext::suitName(snapshot.trump.suit) };
}

scoreboard::Shown scoreboard::shownFor(const GameSnapshot& snapshot)
{
    return { headerFor(snapshot), tensionFor(snapshot), rowsFor(snapshot), trumpFor(snapshot) };
}

scoreboard::Columns scoreboard::columnsFor(const std::vector<Row>& rows)
{
    const Styles& styles = measuring();

    const auto head = [&](const QString& text) { return widthOf(text, styles.head); };
    const auto cell = [&](const QString& text) { return widthOf(text, styles.cell); };

    // Each column's widest content. The numbers at their widest values - one digit
    // for a bid or a trick count; a minus and three digits for a score, which goes
    // negative and stays under a thousand; two digits for a run, which the engine
    // goes on counting past the fifth round, the only one it pays for
    // (Scoreboard.cpp) - so the columns hold still as the numbers change.
    Columns widest;

    widest.player = head(headingPlayer());
    widest.bid = std::max({ head(headingBid()), cell(QStringLiteral("8")), cell(hudtext::dash()) });
    widest.taken = std::max({ head(headingTaken()), cell(QStringLiteral("8")), cell(hudtext::dash()) });
    widest.points = std::max(head(headingPoints()), cell(QStringLiteral("-000")));
    widest.run = std::max({ head(headingRun()),
                            cell(QCoreApplication::translate("scoreboard", "W%1").arg(88)),
                            cell(QCoreApplication::translate("scoreboard", "L%1").arg(88)) });

    for(const Row& row : rows)
    {
        widest.player = std::max(widest.player, cell(turnMarker + row.name));
        widest.bid = std::max(widest.bid, cell(row.bid));
        widest.taken = std::max(widest.taken, cell(row.taken));
        widest.points = std::max(widest.points, cell(row.points));
        widest.run = std::max(widest.run, cell(row.run));
    }

    // The gap on each number column's left: they are right-aligned, so that is the
    // room between it and the column before.
    for(qreal* number : { &widest.bid, &widest.taken, &widest.points, &widest.run })
        *number += columnGap;

    // Names too long for the mock-up's width: the numbers at their widest, and the
    // names what is left of it.
    const qreal numbers = widest.bid + widest.taken + widest.points + widest.run;

    if(widest.player + numbers >= widestContent)
    {
        Columns columns = widest;
        columns.player = std::max(0.0, widestContent - numbers);
        return columns;
    }

    // Columns narrower than the tension at its widest: the room over shared in
    // proportion, as the browser shares out a table's.
    //
    // MEASURED ONCE A RUN: four sentences at their widest numbers, which change
    // with neither the game nor the theme. The translation that words them is
    // installed in main(), before there is a table to draw.
    static const qreal least = widestTension(styles.tension);
    const qreal sum = widest.sum();

    if(sum >= least)
        return widest;

    const qreal share = least / sum;

    return { widest.player * share, widest.bid * share, widest.taken * share,
             widest.points * share, widest.run * share };
}

namespace
{

// sizeFor() for columns already worked out.
QSizeF sizeWith(const scoreboard::Shown& shown, const scoreboard::Columns& columns, const CardRenderer& cards)
{
    const qreal height = layOut(shown, columns, measuring(), Canvas{ nullptr, nullptr, &cards, 1.0 });

    return QSizeF(columns.sum() + 2.0 * (borderWidth + paddingX), height);
}

}

QSizeF scoreboard::sizeFor(const Shown& shown, const CardRenderer& cards)
{
    return sizeWith(shown, columnsFor(shown.rows), cards);
}

// ---- the item ----

ScoreboardItem::ScoreboardItem(const CardRenderer* _cards, panel::Cache* _panels, QGraphicsItem* parent)
    : QGraphicsObject(parent), cards(_cards), panels(_panels ? _panels : &ownPanels)
{
}

void ScoreboardItem::setSnapshot(const GameSnapshot& snapshot, const Theme& palette)
{
    scoreboard::Shown now = scoreboard::shownFor(snapshot);

    // REPAINTED ONLY WHEN IT CHANGES, for SeatItem::setSeat()'s reason: this runs
    // on every snapshot, and an update() of a panel that shows the same as before
    // repaints it, and what is beneath it, for nothing.
    if(now == showing && theme.table == palette.table && !panelSize.isEmpty())
        return;

    theme = palette;

    scoreboard::Columns laid = scoreboard::columnsFor(now.rows);
    const QSizeF size = cards ? sizeWith(now, laid, *cards) : QSizeF();

    if(size != panelSize)
    {
        prepareGeometryChange();
        panelSize = size;
    }

    showing = std::move(now);
    columns = laid;
    memo.invalidate();
    update();
}

QRectF ScoreboardItem::boundingRect() const
{
    if(panelSize.isEmpty())
        return QRectF();

    return QRectF(QPointF(), panelSize).marginsAdded(panel::reach());
}

QPainterPath ScoreboardItem::shape() const
{
    if(panelSize.isEmpty())
        return QPainterPath();

    return panel::shape(QRectF(QPointF(), panelSize));
}

void ScoreboardItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

void ScoreboardItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(panelSize.isEmpty())
        return;

    const panel::Picture& made = picture(devicepixels::effectiveRatio(*painter));

    if(made.pixmap.isNull())
        return;

    painter->drawPixmap(devicepixels::snapped(*painter, made.offset), made.pixmap);
}

const panel::Picture& ScoreboardItem::picture(qreal ratio) const
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
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QPointF(0.0, 0.0), frame.pixmap);

        // The panel's own units from here, its top-left at the origin.
        painter.translate(-frame.offset);

        paintContent(painter, ratio);
        painter.end();

        return composed;
    });
}

void ScoreboardItem::paintContent(QPainter& painter, qreal ratio) const
{
    layOut(showing, columns, stylesFor(theme.table), Canvas{ &painter, &theme.table, cards, ratio });
}
