#ifndef HAND_LAYOUT_H
#define HAND_LAYOUT_H

// Ported from romanian_whist_terminal, src/HandLayout.{h,cpp}.
//
// How a hand is presented, and the way back from what the player picked to what
// the engine holds.
//
// Three orderings meet here and no two of them coincide:
//
//   1. the hand as the engine holds it - deal order, and what playCard()'s
//      index must refer to;
//   2. display order - trump suit first, then a fixed alternating-colour order,
//      high rank first within a suit;
//   3. the legal subset, which is what the player may actually pick from.
//
// Carrying positions rather than cards is what keeps the display translatable
// back to the hand. Getting it wrong means the player clicks one card and plays
// another, which no rule in the engine would object to.
//
// WHAT CHANGED FROM THE TERMINAL'S VERSION: it returns `choices`, the hand
// positions of the numbered legal subset, because a terminal player types a
// number. A click lands on a display slot instead, so this client needs the
// mapping over the WHOLE hand - which HandLayout.cpp already computes as a
// local named `ordered`, sorts by, and then throws away. That local is kept
// here as `positions`; `choices` and HandEntry::choice go with the typed
// number.

#include "GameSnapshot.h"

#include <romanian_whist/Card.h>

#include <cstddef>
#include <optional>
#include <vector>

struct HandLayout
{
    // Every card in the hand, in display order, whether it can be played or
    // not: an illegal card is still shown, just greyed.
    std::vector<HandEntry> entries;

    // positions[i] is the index in the ENGINE's hand of the card at display
    // slot i, so entries and positions are parallel.
    //
    // `choices` does not substitute and is gone with the typed number it
    // existed for: it covered the legal subset only, and a display slot that
    // cannot be played still has to know which card it is drawing.
    std::vector<std::size_t> positions;
};

// `legalCards` empty means nothing is being chosen - a bidding screen, or the
// hand drawn between turns - so every card reads as legal and nothing is greyed
// out. That is what GameSnapshot asks for; the move provider passes a real
// legal set when it is asking for a card.
//
// `trump` is empty in 8-card rounds, which have no trump.
HandLayout layOutHand(const std::vector<romanian_whist::Card>& hand,
                      const std::vector<romanian_whist::Card>& legalCards,
                      const std::optional<romanian_whist::Card>& trump);

#endif
