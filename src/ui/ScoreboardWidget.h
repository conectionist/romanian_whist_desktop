#ifndef SCOREBOARD_WIDGET_H
#define SCOREBOARD_WIDGET_H

#include "core/GameSnapshot.h"
#include "paint/Theme.h"

#include <QCoreApplication>
#include <QString>
#include <QFont>
#include <QFontMetricsF>
#include <QWidget>

#include <vector>

// Section 12's scoreboard: one row per seat, and what players actually read
// between plays.
//
// NEVER ANIMATED, which section 12 states as a rule rather than a preference -
// "a number that slides is a number you cannot read". Nothing in here moves;
// phase E's animation budget is spent on the table beside it.
namespace scoreboard
{

// Translated through QCoreApplication::translate() with an explicit context,
// for both reasons InfoStrip.h gives: this is chrome a player reads rather than
// the log's fixed ASCII, and Q_DECLARE_TR_FUNCTIONS cannot be used in a
// namespace while a wrapper of our own would hide the strings from lupdate.

// One row, already spelled. The widget places these and colours them; every
// decision about WHAT a cell says is made here, where it can be tested without
// a window.
struct Row
{
    // Position in this round's bidding, 1 for whoever opened it. Zero is "not
    // known yet" and renders blank rather than as a seat numbered nought.
    unsigned int bidOrder = 0;

    QString name;
    bool isHuman = false;

    // A DASH, NOT A ZERO, until this seat has bid. hasBet exists on the
    // snapshot precisely to tell a genuine bid of zero from no bid at all, and
    // a scoreboard that reads `bet` without it shows every seat bidding nothing
    // and sitting exactly on target for the whole of the bidding.
    QString bid;

    QString won;

    // Empty until the round is scored, and signed once it is. A round score is
    // zero exactly while nothing has been scored - the engine's
    // calculateRoundScore() returns 5 + bid or a negative number, never zero -
    // so "+0" would be a value this column can never legitimately show, and it
    // would read as a seat that scored nothing rather than as a round still
    // being played.
    QString roundScore;

    // Which way roundScore went, for the colour. Separate from the string
    // because section 18 wants the sign AND the colour, and the widget should
    // not be parsing back a `+` it was just handed.
    int roundScoreSign = 0;

    QString total;

    // Straight off SeatView. The pips are drawn rather than spelled, so these
    // stay numbers here.
    unsigned int winStreak = 0;
    unsigned int lossStreak = 0;
};

std::vector<Row> rowsFor(const GameSnapshot& snapshot);

// The column headings, in the order the widget draws them.
QString headingBid();
QString headingWon();
QString headingRound();
QString headingTotal();

} // namespace scoreboard

// PAINTED, not a grid of QLabels, which is the same call SeatItem makes and for
// overlapping reasons: section 18 wants tabular right-aligned figures and pips
// drawn as pips, and a label per cell would relayout on every one of a game's
// ~1300 frames.
//
// THE COST IS REAL AND WORTH WRITING DOWN: painted text is not selectable and
// not reachable by a screen reader. Section 20's accessibility pass is phase E
// and will have to answer for this table either way, since the card table
// beside it has the same property and no alternative at all.
class ScoreboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScoreboardWidget(QWidget* parent = nullptr);

    void setSnapshot(const GameSnapshot& snapshot);
    void setTheme(const Theme& theme);

    QSize sizeHint() const override;

    // WHAT THE PANEL WILL SHRINK TO, which it must be able to do. sizeHint() is
    // sized for the widest name at the table, and with a Fixed policy that width
    // is taken from the table whatever the window is - at a small logical size
    // the scoreboard was half the window and the green was squeezed into what
    // was left. Section 12 has the table as the region everything else is
    // arranged around, so this one yields and elides its names instead.
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // THE FONT THE PANEL IS BOTH MEASURED AND DRAWN IN. Section 18's tabular
    // figures are applied here and nowhere else.
    //
    // ONE FONT for the same reason there is one columnsFor(): the function was
    // already shared between paintEvent() and the two size hints, and it was
    // still being handed two different fonts - tabular metrics when painting,
    // the plain widget font when measuring. A shared function fed different
    // inputs is the same defect as two copies of the arithmetic, wearing a
    // tidier shape. Where the two fonts differ in digit width the painted
    // columns overrun the width the hint reserved, and the name column - which
    // is whatever is left after them - is what pays for it.
    QFont numberFont() const;

    // The widest name at the table, measured IN THE WEIGHT IT WILL BE DRAWN IN.
    // The human's row is bold, and measuring it plain made the panel ask for a
    // width its own paint then elided the name inside.
    qreal widestNameWidth(const QFont& base) const;

    // How wide each numeric column has to be, for THIS font and THESE headings.
    //
    // Derived rather than constant, and that is a translation requirement rather
    // than a refinement: a column sized in digit widths is sized for its numbers
    // and not for the word above them, so "Round" - five letters over a column
    // built for four digits - drew straight across "Won" beside it. A Romanian
    // build changes every one of those lengths, so the only width that stays
    // right is one measured from the heading actually being drawn.
    //
    // ONE FUNCTION, called by sizeHint() and by paintEvent(). They had parallel
    // copies of this arithmetic and the copies drifted, which is what put the
    // headings a column to the left of their numbers.
    struct Columns
    {
        qreal bid = 0.0;
        qreal won = 0.0;
        qreal round = 0.0;
        qreal total = 0.0;
        qreal streak = 0.0;

        qreal sum() const { return bid + won + round + total + streak; }
    };

    Columns columnsFor(const QFontMetricsF& metrics) const;

    Theme theme = Theme::light();

    std::vector<scoreboard::Row> rows;
};

#endif
