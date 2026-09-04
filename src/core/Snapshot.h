#ifndef SNAPSHOT_H
#define SNAPSHOT_H

// Ported from romanian_whist_terminal, src/GameView.cpp - refreshFromEngine()
// and its rebuildTable() helper, against GameSnapshot instead of GameView.
//
// See QT_CLIENT_PLAN_proposal1.md section 9 for whether the engine-derivable
// half of this belongs in the engine as romanian_whist::snapshotFrom(). That
// question is still open; until it is answered each client keeps its own copy,
// and tests/SnapshotTests.cpp is what stops the two drifting.

#include "GameSnapshot.h"

#include <romanian_whist/GameEngine.h>

// Refills everything readable straight from the engine: seats, scores, bets,
// streaks, hand sizes, round and trick counts, trump and round type - and the
// trick on the table, which the engine holds rather than the client
// accumulating it card by card.
//
// A pure function of the engine. What is left to the caller is only what the
// engine does not model: which seat is active, and which seat is the human's.
//
// snapshot.humanSeat MUST BE SET BEFORE THIS IS CALLED. Every canSeeHand()
// question below needs a viewer, and leaving it at its -1 default means no
// hand is ever filled - the table renders correctly and the player never sees
// a card.
void refreshFromEngine(GameSnapshot& snapshot, const romanian_whist::GameEngine& game);

#endif
