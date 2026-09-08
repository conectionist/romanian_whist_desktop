#ifndef SNAPSHOT_LOG_H
#define SNAPSHOT_LOG_H

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"

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

// THE OTHER HALF OF PHASE C'S RECORD, and the more informative one. A snapshot's
// hand carries no legality and is empty in a blind round; the prompt carries
// both the legal set and the way back to the engine's own hand positions, and it
// is the only object in the program that ever does (see engine/Prompts.h). So
// what a player was actually asked is logged from the prompt, and
// format(GameSnapshot) is deliberately left alone rather than grown a hand
// column that would be a worse copy of this one.
//
// No elapsed argument: a prompt is raised BETWEEN snapshots, and what a reader
// wants from these lines is which question was asked, not how long the gap
// before it was. The snapshot lines either side already carry the clock.
QString format(const BetPrompt& prompt);
QString format(const PlayPrompt& prompt);

// THREE ENTRY POINTS, AND THAT IS THE WHOLE LIST. This said "one function,
// deliberately" until C1, and the argument it was making survives unchanged:
// what must not be exported from here are the two SPELLINGS - the card and the
// round type - which were exported once and taken back. The card because tests
// hardcoded the strings anyway, the round type because MainWindow's status bar
// cannot share fixed ASCII a log test asserts on with text that has to go
// through tr(); the comment on infostrip::roundTypeText() says why two switches
// over RoundType is the right number.
//
// The three formatters below share both spellings and export neither, which is
// the property that comment was protecting. A fourth would be welcome on the
// same terms.

} // namespace snapshotlog

#endif
