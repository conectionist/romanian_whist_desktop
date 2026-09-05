#ifndef SNAPSHOT_LOG_H
#define SNAPSHOT_LOG_H

#include "core/GameSnapshot.h"

#include <QString>

#include <chrono>

// One line of phase A's log, from one snapshot.
//
// A free function rather than a MainWindow member, for two reasons. It is the
// only part of the logging window with a right answer, so it is the only part
// worth a test - and phase B demotes the widget behind a View menu, at which
// point a formatter living inside MainWindow would be rewritten along with
// everything around it.
//
// THE TIMESTAMP IS NOT DECORATION. Phase A's open question is where two pacing
// waits belong, and the build order allows for a text log being too coarse to
// answer it. With elapsed time on every line it is not coarse at all: a 1.2s
// gap sits in the text, against the line it followed, and the question is read
// rather than felt.
namespace snapshotlog
{

// `elapsed` is measured from the first snapshot, not from process start: what
// matters is the gap between frames, and a constant offset only makes the
// numbers harder to subtract by eye.
QString format(const GameSnapshot& snapshot, std::chrono::milliseconds elapsed);

// The card as this log spells it - rank then suit, no separator, matching
// Card::toString(). Exposed for the tests, which would otherwise have to
// hardcode the same strings twice.
QString describe(const romanian_whist::Card& card);

} // namespace snapshotlog

#endif
