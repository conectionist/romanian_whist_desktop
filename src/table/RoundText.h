#ifndef ROUND_TEXT_H
#define ROUND_TEXT_H

#include "core/GameSnapshot.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/RoundType.h>

#include <QString>

// THE INFO STRIP'S WORDS: the round, the cards dealt, the round type and the
// trump. The strip ran across the top of the window until the table redesign's
// phase 8 merged it into the scoreboard (section 2.9), which now says all four.
// Its words are here, under their old name and their old translation context, in
// a header of their own: the scoreboard says them, and so does the round sheet's
// title, which names the same round and has no use for the scoreboard's item.
//
// TRANSLATED, which is the opposite of snapshotlog's choice and for the reason
// that file gives: its words are asserted on by tests and read out of saved logs,
// so they must not move when a translation is loaded. These are chrome a player
// reads, so they must.
//
// Spelled as QCoreApplication::translate() with an explicit context at every call
// site, NOT through a tr() of our own. Q_DECLARE_TR_FUNCTIONS is the obvious tool
// and does not work here - it expands with `public:` and `private:` inside it, so
// it is a class-only macro whatever its name suggests. A hand-rolled inline tr()
// wrapper compiles and is worse: lupdate finds strings by recognising the CALL, so
// a wrapper hides every one of them from the translation sweep.
namespace infostrip
{

// What the strip said, from one snapshot. The trump is handed over as a CARD
// rather than as text: this struct is the seam between "which card" and "how it
// is drawn".
struct Fields
{
    QString round;      // "Round 7 of 24"
    QString cards;      // "6 cards"
    QString roundType;  // "Normal", through tr()

    // hasTrump is false in the eight-card round, which has no trump at all -
    // and `trump` is then Card's default, which is a REAL two of clubs.
    // SnapshotLog.cpp carries the same trap; drawing it unguarded is how the
    // scoreboard would claim a trump that does not exist.
    bool hasTrump = false;
    romanian_whist::Card trump{};
};

Fields fieldsFor(const GameSnapshot& snapshot);

// The round type as a PLAYER reads it. THE SECOND SPELLING, NOT THE THIRD:
// snapshotlog has fixed ASCII for the reason above, and that duplication is
// deliberate. The client has exactly two: one for machines and one for people.
QString roundTypeText(romanian_whist::RoundType type);

// "6 cards", or "1 card". Shared with the round sheet, which names the same fact
// about the same round in its title - two spellings of it would be two entries in
// the catalogue and two chances for them to drift. See the body for why this is
// two strings rather than Qt's "%n card(s)".
QString cardsText(unsigned int trickCount);

// WHETHER THIS ROUND TYPE IS CALLED OUT, which section 17 asks for: Forehead and
// Hidden read "in an accent colour, so the round is identifiable before the
// cards are dealt".
//
// NEVER A COLOUR-ONLY CUE, which is section 18's rule and the reason this is
// safe: the WORD is already the distinguishing information and is already on
// screen, so the colour reinforces it rather than carrying it.
bool isCalledOut(romanian_whist::RoundType type);

} // namespace infostrip

#endif
