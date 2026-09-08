#ifndef PROMPT_STAND_IN_H
#define PROMPT_STAND_IN_H

#include "engine/Prompts.h"

#include <cstddef>

// TEMPORARY, AND DELETED IN C3. This file has a shorter life than anything else
// in the client, and that is deliberate rather than a failure to finish.
//
// C1 seats a human, which fills that seat's provider with a SeatProvider and
// makes the engine park in makeBet() waiting for a click. C2 builds the panel
// that answers the bet and C3 the hand that answers the card - so between here
// and there the engine parks at the first bid of the first round and the client
// cannot reach its own second round, let alone the Forehead and Hidden rounds
// that end every default schedule and that section 8 says are where a
// snapshot-driven hand stops working.
//
// So C1 answers the prompts itself, with the least code that can: the lowest
// legal bid and the first legal card. That buys the property the whole build
// order is ordered for - one plausible cause. With the seam proven end to end
// before a widget exists, a stall in C3 or C4 is the widget and nothing else,
// where the two landing together would leave "the hand drew nothing" and "the
// prompt never arrived" indistinguishable. It is the argument that puts phase A
// before phase B, applied inside phase C.
//
// IT IS NOT A BOT and must never grow into one. The engine already has
// strategies for that, and a stand-in that played well would be a thing someone
// could be tempted to keep. This picks the first thing it is allowed to pick.
//
// Two free functions rather than logic inside a slot, on the grounds SnapshotLog
// gives for being one: this is the part with a right answer, so it is the part
// worth a test, and it needs no widget, no thread and no engine to have that
// answer checked.
//
// C2 stops calling chooseBet(), C3 stops calling chooseSlot() and removes the
// file.
namespace promptstandin
{

// The lowest bid this prompt permits: 0, or 1 where 0 is the forbidden one.
//
// NEVER prompt.forbiddenBet. The engine's response to an illegal bet is to throw
// out of run(), which reaches the player as "the game thread failed" with the
// game unresumable - QtMoveProvider::makeBet() re-prompts rather than passing
// one on, so the visible cost of getting this wrong is a prompt loop rather than
// a crash, but it is still a loop.
unsigned int chooseBet(const BetPrompt& prompt);

// The DISPLAY SLOT to click - the first one marked legal.
//
// A slot, not a hand index, because that is what a click produces and the caller
// owes the translation back through PlayPrompt::handIndex exactly as C3's click
// handler will. Returning the index here would skip the one step this stand-in
// is best placed to exercise.
//
// In a blind round `entries` is empty and every slot is playable, so that is
// slot 0 - which is also the only slot, both blind rounds being 1-trick ones.
std::size_t chooseSlot(const PlayPrompt& prompt);

} // namespace promptstandin

#endif
