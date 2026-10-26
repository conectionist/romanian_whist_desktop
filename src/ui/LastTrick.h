#ifndef LAST_TRICK_H
#define LAST_TRICK_H

#include "core/GameSnapshot.h"

#include <romanian_whist/Card.h>

#include <QString>

#include <optional>
#include <vector>

// WHAT THE LAST TRICK SHEET SHOWS (the table redesign's phase 11), and what the
// window keeps of the table for it. Apart from the sheet, LastTrickSheet.h, so that
// MainWindow.h, which keeps a Tracker, takes in no widget's header for it (PR #37's
// review, finding #10).
namespace lasttrick
{

// One card of a trick as the sheet shows it.
struct Played
{
    romanian_whist::Card card{};
    unsigned int seatIndex = 0;
    QString name;

    // The engine's ranking, never recomputed (PlayedCardView::isWinning): on a
    // finished trick, the card that took it.
    bool isWinning = false;
};

struct Trick
{
    // The round it was won in, 1-based as the snapshot counts.
    unsigned int roundNumber = 0;

    // Your seat, or -1 when you are watching: whose win is "You won it".
    int humanSeat = -1;

    // In play order, the leader's first.
    std::vector<Played> cards;
};

// WHAT THE WINDOW KEEPS OF THE TABLE: the last trick finished, in whichever round,
// read off the frames as they pass.
//
// A FINISHED TRICK IS A FRAME WITH A CARD FROM EVERY SEAT. The bridge publishes one
// as the trick is won, while it is still the engine's current trick
// (GameBridge::onTrickWon()), and its isWinning is the engine's winner. Taken the
// moment it is complete, so while it is still on the felt the sheet shows that
// same trick; the frames of the next trick, partly played, leave it be.
//
// KEPT ACROSS ROUNDS, by the owner's call of 2026-10-22: "Last trick" reads as the
// last trick played, and at the start of a round that is the last round's final
// trick. The mock-up forgets it when it deals, and so did phase 11 at first, which
// left the button saying nothing had finished at every round's start. Only a new
// game forgets it, through clear().
class Tracker
{
public:
    void observe(const GameSnapshot& snapshot);
    void clear() { kept.reset(); }

    const std::optional<Trick>& last() const { return kept; }

private:
    std::optional<Trick> kept;
};

} // namespace lasttrick

#endif
