#ifndef CARD_ORDER_H
#define CARD_ORDER_H

// Ported from romanian_whist_terminal, src/CardFormat.{h,cpp} - the three
// display-ordering members of CardFormat, lifted out as free functions.
//
// CardFormat itself does not come with them: it holds a const Terminal& to
// decide glyphs and colour, and neither has a meaning in a GUI. What is kept is
// the part that is pure - which suit sorts where, and which rank sorts first -
// so that a card sits in the same slot in both clients.

#include <romanian_whist/Card.h>

#include <optional>
#include <vector>

namespace cardorder
{

// Where a suit sorts. 0 for the trump suit, then a fixed alternating red/black
// order so neighbouring groups do not blur together.
unsigned int suitPosition(romanian_whist::Suit suit,
                          const std::optional<romanian_whist::Card>& trump);

// A strict weak ordering over cards: trump suit first, then the fixed suit
// order, and high rank before low inside a suit.
//
// No two cards in one hand ever compare equivalent - suit position is injective
// over suits, and a hand holds no two cards of the same suit and rank - so an
// unstable std::sort still produces one fixed order.
bool inDisplayOrder(const romanian_whist::Card& left,
                    const romanian_whist::Card& right,
                    const std::optional<romanian_whist::Card>& trump);

void sortForDisplay(std::vector<romanian_whist::Card>& cards,
                    const std::optional<romanian_whist::Card>& trump);

} // namespace cardorder

#endif
