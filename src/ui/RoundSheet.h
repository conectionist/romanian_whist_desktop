#ifndef ROUND_SHEET_H
#define ROUND_SHEET_H

#include "engine/RoundResult.h"
#include "ui/Overlay.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QGridLayout;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

// Section 16.1's round sheet: what the round just scored was worth, per seat,
// over the table it was played on.
//
// Translated through QCoreApplication::translate() with an explicit context, for
// the reason table/ScoreboardItem.h gives at length - this is chrome a player
// reads, Q_DECLARE_TR_FUNCTIONS is class-only, and a tr() wrapper of our own
// hides every string in here from lupdate.
namespace roundsheet
{

// "Round 7 - 6 cards - Normal". The three facts that identify a round, and the
// same three the scoreboard carries at the top of the table all round.
QString titleFor(const RoundResult& result);

// SECTION 16.1's "34 -> 41", and the honest rendering of a number that means two
// different things either side of a commit. Inside onRoundScored() the total is
// what this round has NOT been folded into yet, so a sheet showing the total
// alone reads differently before and after; showing both needs no explanation.
QString arrowFor(const RoundSeatResult& seat);

// The round score with its sign, which is what carries the meaning - section 18
// allows no cue that is colour alone, and the widget colours this as well.
QString roundScoreFor(const RoundSeatResult& seat);

// SECTION 16.1's WARNING LINES, and the reason RoundResult exists rather than
// the sheet reading a snapshot. The engine awards +/-10 on the FIFTH consecutive
// exact bid or miss, and a player has no way to see that coming without counting
// rounds.
//
// ONE LINE PER SEAT ON FOUR, in table order, and D2 shipped this returning after
// the first: a second player on four is exactly as entitled to the warning as
// the first, and the round where two of them are is the round it matters most.
// The early return also let an earlier seat's WIN streak silence a later seat's
// LOSS streak, which is the half no eye would have caught.
//
// FOUR MEANS THE NEXT ONE IS THE FIFTH. Scoreboard::calculateScores() updates
// the streaks before onRoundScored() fires, so the counts in the result already
// include the round being reported.
//
// AND IT IS SILENT WHEN THE NEXT ROUND DEALS ONE CARD, which is the whole point:
// one-card rounds count for neither direction of a streak - they neither advance
// one nor break it - so "a fifth costs 10" would simply be false. That fact
// belongs to the NEXT round and no snapshot carries it; RoundResult does,
// because only the game thread could ask.
//
// Empty when nobody is on four, which is most rounds.
QStringList streakWarnings(const RoundResult& result);

} // namespace roundsheet

// The widget. An Overlay: a child of the window's stack rather than a QDialog,
// so it reads as modal without blocking the window that section 4.4 needs
// closable.
//
// ITS BUTTON SAYS "Continue" IN EVERY ROUND, where section 16.1's mockup says
// "Deal round 8". The last round has nothing left to deal - the sheet there
// hands over to the game-over panel - and a label that is a lie on the final
// round is worse than one word that is true in all of them. Section 16.1 is
// amended to record it.
class RoundSheet : public Overlay
{
    Q_OBJECT

public:
    explicit RoundSheet(QWidget* parent = nullptr);

    // Fills the sheet from one round. Rebuilt per round rather than updated in
    // place: the seat count is fixed for a game but the rows carry per-round
    // numbers throughout, and BidStrip's reason applies - a panel built once and
    // rewritten is a panel whose stale half is invisible.
    void showResult(const RoundResult& result);

    // Continue, so Enter dismisses.
    void focusDefault() override;

signals:
    void continueRequested();

private:
    // Empties the grid COMPLETELY, headings included - see the body for why they
    // are rebuilt per round rather than kept.
    void clearRows();

    void addHeadings();
    void addRow(const RoundSeatResult& seat);

    QLabel* title = nullptr;
    QWidget* rows = nullptr;
    QGridLayout* rowLayout = nullptr;

    // Hidden when nothing is on four, which is most rounds - and hidden rather
    // than emptied, because this sheet is rebuilt per round and is not the
    // fixed-height info strip whose B2b finding argues the other way.
    QLabel* warning = nullptr;

    QPushButton* continueButton = nullptr;

    int placedRows = 0;
};

#endif
