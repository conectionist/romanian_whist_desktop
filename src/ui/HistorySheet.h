#ifndef HISTORY_SHEET_H
#define HISTORY_SHEET_H

#include "engine/RoundResult.h"
#include "ui/Overlay.h"

#include <QString>

#include <cstddef>
#include <vector>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// THE ROUND HISTORY (the table redesign's phase 11): every round of the game so
// far, one row each, and what it was worth to every seat. The mock-up's
// showHistory() - Round, Cards, then a column per player - and the scoresheet
// QT_CLIENT_PLAN_proposal1.md section 16.3 asked for, without its Type and Trump
// columns, which the mock-up does not have.
//
// FROM THE RoundResults THE WINDOW KEEPS, one per round as onRoundScored()
// delivers it. The engine stores no per-round score, so these are the only record
// of one (RoundResult.h).
//
// Translated through QCoreApplication::translate() under the context
// "historysheet", for the reason table/ScoreboardItem.h gives.
namespace historysheet
{

// THE SMALL MUTED PART OF A CELL, after the round's score: tricks taken, then the
// bid, "1/2" - TAKEN FIRST, as the seat's pill reads "Taken/Bid", by the owner's
// call of 2026-10-22 (the mock-up writes bid/taken). A slash reads as progress, x
// out of y. A dash for a seat that never bid.
QString noteFor(const RoundSeatResult& seat);

// EVERY SEAT'S TOTAL AFTER THE LAST ROUND SCORED, in seat order: that round's
// committed total plus what it was worth, the engine's two numbers (RoundResult.h)
// rather than a sum of the rows here - a row this client lost would otherwise
// change the total. Zeros, `seats` of them, before any round is scored.
std::vector<int> totalsFor(const std::vector<RoundResult>& rounds, std::size_t seats);

} // namespace historysheet

// The widget, an Overlay like every sheet the menu bar opens: over everything, and
// holding a game in play while it is up. Opened from the table's Round history
// button, the round sheet's and the standings'.
class HistorySheet : public Overlay
{
    Q_OBJECT

public:
    explicit HistorySheet(QWidget* parent = nullptr);

    // Fills the sheet from the game's rounds so far. `names` heads the columns, in
    // seat order: from the table, so a game with no round scored yet still shows
    // who is playing. Empty, the last round's names are taken.
    void showHistory(const std::vector<RoundResult>& rounds, const std::vector<QString>& names);

    // Close, so Enter closes - and the foot of the sheet in view, when it is taller
    // than the page: the latest round, the totals and Close.
    void focusDefault() override;

signals:
    void closeRequested();

protected:
    // A narrower page narrows the sheet, and a scroll bar narrows its body: the
    // names give way again to fit.
    void contentWidthChanged() override;

private:
    // Shortens the names in the headings until the table fits the sheet's content
    // width (Overlay::contentWidth()).
    void fitColumns();

    QLabel* title = nullptr;
    QLabel* meta = nullptr;
    SheetTable* rows = nullptr;
    SheetButton* closeButton = nullptr;
};

#endif
