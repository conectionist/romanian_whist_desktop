#ifndef CARD_TEXT_H
#define CARD_TEXT_H

#include <romanian_whist/Card.h>

#include <QString>

// A CARD AS A PLAYER READS IT IN A SENTENCE: the trick panel's "Winning: Costel,
// K♥" and the scoreboard's "♣ Clubs". Not the log's, which is Card::toString()'s
// fixed ASCII ("KH") for the reason snapshotlog gives; this is the spelling for
// people, and it is translated.
//
// THE GLYPHS ARE NOT TRANSLATED and need no colour here: a ♥ is a ♥ in every
// language, and textruns paints it red wherever it lands.
namespace cardtext
{

// ♥ ♦ ♠ ♣. Section 15.2's own spelling - "You must follow ♥", not "You must
// follow hearts" - and the one that needs no translation.
QString symbol(romanian_whist::Suit suit);

// "Hearts", "Diamonds", "Spades", "Clubs": the trump's name under its card.
QString suitName(romanian_whist::Suit suit);

// The rank and the suit's glyph: "K♥", "10♠", "A♦". The court cards' letters are
// translated - a Romanian deck's king is an R.
QString label(romanian_whist::Card card);

} // namespace cardtext

#endif
