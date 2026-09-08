#include "ScoreboardWidget.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>

namespace
{

// The columns, in drawing order, as multiples of the digit width. The name
// column takes whatever is left, which is what lets a six-seat game with long
// names sit in the same panel as a two-seat game with short ones.
constexpr qreal orderColumnInDigits = 2.4;

// The FLOOR for a numeric column, in digit widths - what the numbers need. The
// heading may want more, and columnsFor() takes whichever is larger.
constexpr qreal numberColumnInDigits = 3.4;
constexpr qreal totalColumnInDigits = 4.2;
constexpr qreal streakColumnInDigits = 4.0;

// Breathing room between one column and the next, so two right-aligned values
// never touch.
constexpr qreal columnPaddingInDigits = 0.9;

// How little of a name the panel will ever show. Below this the column is an
// ellipsis and nothing else, which tells a player nothing at all.
constexpr qreal minimumNameInDigits = 4.0;

// Everything vertical, in text lines.
constexpr qreal rowHeightInLines = 1.6;
constexpr qreal headerHeightInLines = 1.9;
constexpr qreal marginInLines = 0.6;

// The engine awards a bonus on the FIFTH consecutive exact bid or miss, so five
// is the whole of what a streak needs to show - and section 16.1 has the round
// sheet warn at four, which only means anything if four pips are countable.
constexpr unsigned int maximumPips = 5;

// A dash, for a seat that has not bid. Punctuation rather than a word, so it is
// not a translatable string; an en dash rather than a hyphen because a hyphen
// beside right-aligned digits reads as a minus.
QString noBid()
{
    return QStringLiteral("–");
}

}

QString scoreboard::headingBid()
{
    return QCoreApplication::translate("scoreboard", "Bid");
}

QString scoreboard::headingWon()
{
    return QCoreApplication::translate("scoreboard", "Won");
}

QString scoreboard::headingRound()
{
    return QCoreApplication::translate("scoreboard", "Round");
}

QString scoreboard::headingTotal()
{
    return QCoreApplication::translate("scoreboard", "Total");
}


std::vector<scoreboard::Row> scoreboard::rowsFor(const GameSnapshot& snapshot)
{
    std::vector<Row> rows;
    rows.reserve(snapshot.seats.size());

    for(const SeatView& seat : snapshot.seats)
    {
        Row row;

        row.bidOrder = seat.bidOrder;
        row.name = QString::fromStdString(seat.name);
        row.isHuman = seat.isHuman;

        // See the header: the dash is what tells "has not bid" from "bid zero",
        // and hasBet is the only thing that knows the difference.
        row.bid = seat.hasBet ? QString::number(seat.bet) : noBid();

        row.won = QString::number(seat.tricksWon);

        row.roundScoreSign = seat.roundScore == 0 ? 0 : (seat.roundScore < 0 ? -1 : 1);

        // The sign is always shown on a scored round, positive included. What is
        // being read here is a delta against the total beside it, and section 18
        // asks for the `+` by name.
        if(seat.roundScore > 0)
            row.roundScore = QStringLiteral("+%1").arg(seat.roundScore);
        else if(seat.roundScore < 0)
            row.roundScore = QString::number(seat.roundScore);

        // NOT summed with roundScore here. Inside onRoundScored() the total is
        // the committed one the round has not yet been folded into, and section
        // 16.1's round sheet shows both sides of that as "34 -> 41" precisely
        // because they differ. A client that added them would be doing the
        // engine's arithmetic, and doing it wrong for one frame a round.
        row.total = QString::number(seat.totalScore);

        row.winStreak = seat.winStreak;
        row.lossStreak = seat.lossStreak;

        rows.push_back(row);
    }

    return rows;
}

QFont ScoreboardWidget::numberFont() const
{
    QFont numbers = font();

    // TABULAR FIGURES (section 18), so a 1 and a 0 occupy the same box and a
    // column of scores lines up on its own without being monospaced.
    //
    // GUARDED, BECAUSE CMakeLists.txt DECLARES A FLOOR OF 6.5 and QFont::Tag
    // and setFeature arrived in 6.7. This comment said the feature was "simply
    // not applied" on anything older, which was the intent and was not true of
    // the code beneath it: an unguarded call does not degrade on 6.5 or 6.6, it
    // fails to compile. The version this is built against is not the version it
    // promises to build against, and only the promise is checkable from here.
    //
    // The fallback is proportional digits - a cosmetic difference, and the
    // reason a guard is the right answer rather than raising the floor for a
    // refinement. Appendix B.6 records 6.5 as a decision taken, not a default
    // left lying around.
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    numbers.setFeature(QFont::Tag("tnum"), 1);
#endif

    return numbers;
}

qreal ScoreboardWidget::widestNameWidth(const QFont& base) const
{
    QFont strong = base;
    strong.setBold(true);

    const QFontMetricsF plain(base);
    const QFontMetricsF bold(strong);

    // THE FLOOR BEFORE THE FIRST SNAPSHOT, in digit widths - the same floor
    // minimumSizeHint() shows, because a panel with no rows is not yet sized for
    // anyone. It was a name once, and a name here is the constant the comment
    // below argues against: "Cristina" is exactly a width big enough for today's
    // players, which is the defect one game later.
    qreal widest = plain.horizontalAdvance(QLatin1Char('0')) * minimumNameInDigits;

    // The widest name this game has, so the panel is sized for the players who
    // are actually at the table. SnapshotLog derives its own column width the
    // same way and for the same reason: a constant big enough for any name
    // spends the space on every game, and one merely bigger than today's names
    // is the same defect again, later.
    //
    // Each name in ITS OWN WEIGHT, because paintEvent() draws the human's bold
    // and bold is wider. Measured plain, the panel asked for a width at which
    // its own paint then elided that name - the one name at the table a player
    // is most sure of.
    for(const scoreboard::Row& row : rows)
        widest = std::max(widest, (row.isHuman ? bold : plain).horizontalAdvance(row.name));

    return widest;
}

ScoreboardWidget::Columns ScoreboardWidget::columnsFor(const QFontMetricsF& metrics) const
{
    const qreal digit = metrics.horizontalAdvance(QLatin1Char('0'));
    const qreal padding = digit * columnPaddingInDigits;

    const auto wide = [&](const QString& heading, qreal digits)
    {
        return std::max(metrics.horizontalAdvance(heading), digit * digits) + padding;
    };

    Columns columns;

    columns.bid = wide(scoreboard::headingBid(), numberColumnInDigits);
    columns.won = wide(scoreboard::headingWon(), numberColumnInDigits);
    columns.round = wide(scoreboard::headingRound(), totalColumnInDigits);
    columns.total = wide(scoreboard::headingTotal(), totalColumnInDigits);

    // No heading of its own - the pips speak for themselves and a word over them
    // would be the widest thing in the panel for the least information in it.
    //
    // The padding is added by hand for exactly that reason: every other column
    // gets it from wide(), which cannot be called without a heading to measure.
    // Without it this was the one column with no breathing room, and the Total
    // digits ended up two pixels from the leftmost pip - which renders as
    // touching.
    columns.streak = digit * streakColumnInDigits + padding;

    return columns;
}

ScoreboardWidget::ScoreboardWidget(QWidget* parent)
    : QWidget(parent)
{
    // Maximum, not Fixed: sizeHint() is the width this WANTS and the most it
    // will ever take, and Qt may take it back down to minimumSizeHint() when the
    // table beside it needs the room.
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
}

void ScoreboardWidget::setTheme(const Theme& palette)
{
    theme = palette;
    update();
}

void ScoreboardWidget::setSnapshot(const GameSnapshot& snapshot)
{
    // Rebuilt every frame rather than compared field by field. A row is a
    // handful of short strings and there are at most six of them, where the
    // info strip's guard exists because a QLabel::setText() is a relayout; a
    // repaint of a painted panel is not, and update() already coalesces.
    rows = scoreboard::rowsFor(snapshot);

    updateGeometry();
    update();
}

QSize ScoreboardWidget::sizeHint() const
{
    // numberFont(), not font(): the width asked for here is the width
    // paintEvent() then lays its columns out in. See the header.
    const QFont numbers = numberFont();
    const QFontMetricsF metrics(numbers);

    const qreal digit = metrics.horizontalAdvance(QLatin1Char('0'));
    const qreal margin = metrics.height() * marginInLines;

    const qreal width = margin * 2.0
                        + digit * orderColumnInDigits
                        + widestNameWidth(numbers) + digit
                        + columnsFor(metrics).sum();

    const qreal height = margin * 2.0
                         + metrics.height() * headerHeightInLines
                         + metrics.height() * rowHeightInLines * std::max<std::size_t>(rows.size(), 4);

    return QSize(qRound(width), qRound(height));
}

QSize ScoreboardWidget::minimumSizeHint() const
{
    const QFontMetricsF metrics(numberFont());

    const qreal digit = metrics.horizontalAdvance(QLatin1Char('0'));
    const qreal margin = metrics.height() * marginInLines;

    // The numbers in full - they are the point of the panel and there is no
    // shorter form of them - plus enough name for a first letter or two and the
    // ellipsis that follows it.
    const qreal width = margin * 2.0
                        + digit * orderColumnInDigits
                        + digit * minimumNameInDigits
                        + columnsFor(metrics).sum();

    return QSize(qRound(width), 0);
}

void ScoreboardWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), theme.seatPlaque);

    // Tabular figures, and the one font both size hints measure in - the
    // argument for that is on numberFont() in the header.
    const QFont numbers = numberFont();

    const QFontMetricsF metrics(numbers);

    const qreal digit = metrics.horizontalAdvance(QLatin1Char('0'));
    const qreal margin = metrics.height() * marginInLines;
    const qreal rowHeight = metrics.height() * rowHeightInLines;

    // ONE SET OF COLUMN EDGES, used by the header and by every row.
    //
    // The first version computed them twice - once for the headings and once for
    // the numbers - in parallel expressions that were nearly the same, and the
    // headings came out shifted a column left: "BidWonoundTotal" across the top
    // of the panel, each heading overlapping the next. Two coordinate systems
    // that must agree is a thing that will not stay agreeing, so there is one.
    //
    // Laid out from the RIGHT, because every column but the name is a number of
    // known width and the name absorbs the remainder.
    struct Column
    {
        qreal right;
        qreal width;
    };

    const Columns widths = columnsFor(metrics);

    const qreal streakRight = width() - margin;

    const Column totalColumn{ streakRight - widths.streak, widths.total };
    const Column roundColumn{ totalColumn.right - widths.total, widths.round };
    const Column wonColumn{ roundColumn.right - widths.round, widths.won };
    const Column bidColumn{ wonColumn.right - widths.won, widths.bid };

    const qreal orderLeft = margin;
    const qreal nameLeft = orderLeft + digit * orderColumnInDigits;
    // Whatever is left after the numbers, which at the minimum width is very
    // little - hence the elide below. Never negative: a panel narrower than its
    // own minimum is not a thing Qt should produce, and clamping is cheaper than
    // finding out what QFontMetricsF does with a negative width.
    const qreal nameWidth = std::max(0.0, bidColumn.right - widths.bid - nameLeft);

    const auto cell = [&](const Column& column, qreal top, const QString& text)
    {
        painter.drawText(QRectF(column.right - column.width, top, column.width, rowHeight),
                         Qt::AlignRight | Qt::AlignVCenter, text);
    };

    painter.setFont(numbers);

    // The header, dimmed against the rows so the eye lands on the numbers.
    QColor heading = theme.textPrimary;
    heading.setAlphaF(0.65f);

    painter.setPen(heading);

    qreal y = margin;

    cell(bidColumn, y, scoreboard::headingBid());
    cell(wonColumn, y, scoreboard::headingWon());
    cell(roundColumn, y, scoreboard::headingRound());
    cell(totalColumn, y, scoreboard::headingTotal());

    y += metrics.height() * headerHeightInLines;

    for(const scoreboard::Row& row : rows)
    {
        painter.setPen(theme.textPrimary);

        if(row.bidOrder > 0)
        {
            painter.drawText(QRectF(orderLeft, y, digit * orderColumnInDigits, rowHeight),
                             Qt::AlignLeft | Qt::AlignVCenter, QString::number(row.bidOrder));
        }

        // Elided, because a name is arbitrary text a setup dialog will supply in
        // phase D and a panel is a fixed width.
        QFont nameFont = numbers;
        nameFont.setBold(row.isHuman);

        painter.setFont(nameFont);
        painter.drawText(QRectF(nameLeft, y, nameWidth, rowHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(nameFont).elidedText(row.name, Qt::ElideRight, nameWidth));

        painter.setFont(numbers);

        cell(bidColumn, y, row.bid);
        cell(wonColumn, y, row.won);

        // The one place the palette's score colours are read. The SIGN is
        // already in the string - section 18 allows no cue that is colour alone.
        if(row.roundScoreSign != 0)
        {
            painter.setPen(row.roundScoreSign > 0 ? theme.scorePositive : theme.scoreNegative);
            cell(roundColumn, y, row.roundScore);
        }

        painter.setPen(theme.textPrimary);
        cell(totalColumn, y, row.total);

        // Streak pips, DRAWN rather than spelled - which is the argument for
        // painting this panel at all. A win streak and a loss streak are never
        // both non-zero, so one loop over whichever is running is the whole of
        // it.
        const bool winning = row.winStreak > 0;
        const unsigned int streak = std::min(winning ? row.winStreak : row.lossStreak, maximumPips);

        if(streak > 0)
        {
            const qreal pip = digit * 0.30;
            const qreal step = pip * 2.6;

            painter.setPen(Qt::NoPen);
            painter.setBrush(winning ? theme.scorePositive : theme.scoreNegative);

            for(unsigned int i = 0 ; i < streak ; i++)
            {
                painter.drawEllipse(QPointF(streakRight - pip - (streak - 1 - i) * step,
                                            y + rowHeight / 2.0),
                                    pip, pip);
            }

            painter.setBrush(Qt::NoBrush);
        }

        y += rowHeight;
    }
}
