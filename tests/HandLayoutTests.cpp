#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "core/HandLayout.h"

#include <cstddef>
#include <optional>
#include <vector>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// Ported from romanian_whist_terminal, tests/HandLayoutTests.cpp. The fixture
// and the display-order expectations are unchanged; every assertion about
// `choices` and HandEntry::choice is rewritten against `positions`, which is
// what this client keeps instead (see core/HandLayout.h).
//
// This function reconciles three orderings and no two of them coincide: the
// hand as the engine holds it, the order the cards are drawn in, and the legal
// subset the player may pick from. Get the mapping wrong and the player clicks
// one card and plays another - which breaks no rule the engine could object to,
// so nothing downstream would notice.

namespace
{
// Deliberately NOT in display order: spades sort first, then hearts, then
// clubs, then diamonds, and rank runs high to low inside a suit. So the display
// order of this hand is A-spades, Q-spades, K-hearts, 7-diamonds - hand
// positions 1, 3, 2, 0, which share no prefix with deal order.
const Card DiamondsSeven(Rank::Seven, Suit::Diamonds);   // hand position 0
const Card SpadesAce(Rank::Ace, Suit::Spades);           // hand position 1
const Card HeartsKing(Rank::King, Suit::Hearts);         // hand position 2
const Card SpadesQueen(Rank::Queen, Suit::Spades);       // hand position 3

const std::vector<Card> Hand{ DiamondsSeven, SpadesAce, HeartsKing, SpadesQueen };

// The invariant the whole function exists to hold: the card drawn in a display
// slot is the card that slot's position names in the hand the engine holds.
//
// Checked over EVERY slot rather than only the legal ones - that is the whole
// difference from the terminal's version, where an unplayable card had no
// number and so no way back at all.
void requireEverySlotLeadsBackToItsCard(const HandLayout& layout, const std::vector<Card>& hand)
{
    REQUIRE(layout.positions.size() == layout.entries.size());

    for(std::size_t i = 0 ; i < layout.entries.size() ; i++)
    {
        REQUIRE(layout.positions[i] < hand.size());
        REQUIRE(hand[layout.positions[i]] == layout.entries[i].card);
    }
}
}

TEST_CASE("Cards are laid out in display order, not the order they were dealt", "[hand-layout]")
{
    const HandLayout layout = layOutHand(Hand, Hand, std::nullopt);

    const std::vector<Card> expected{ SpadesAce, SpadesQueen, HeartsKing, DiamondsSeven };

    REQUIRE(layout.entries.size() == expected.size());

    for(std::size_t i = 0 ; i < expected.size() ; i++)
        REQUIRE(layout.entries[i].card == expected[i]);

    // And the positions it hands back are into the hand, which is in a
    // different order than what was just drawn.
    REQUIRE(layout.positions == std::vector<std::size_t>{ 1, 3, 2, 0 });

    requireEverySlotLeadsBackToItsCard(layout, Hand);
}

TEST_CASE("Trump leads the display order", "[hand-layout]")
{
    // Diamonds are trump, so the seven that sorted last now sorts first, and
    // every position it hands back shifts with it.
    const Card trump(Rank::Two, Suit::Diamonds);

    const HandLayout layout = layOutHand(Hand, Hand, trump);

    REQUIRE(layout.entries.front().card == DiamondsSeven);
    REQUIRE(layout.positions == std::vector<std::size_t>{ 0, 1, 3, 2 });

    requireEverySlotLeadsBackToItsCard(layout, Hand);
}

TEST_CASE("Illegal cards are greyed but still drawn, and still addressable", "[hand-layout]")
{
    // Following spades: the two spades are legal, the other two are not.
    const std::vector<Card> legal{ SpadesAce, SpadesQueen };

    const HandLayout layout = layOutHand(Hand, legal, std::nullopt);

    // Every card appears, whether it can be played or not - an illegal card is
    // greyed, not hidden.
    REQUIRE(layout.entries.size() == Hand.size());

    REQUIRE(layout.entries[0].legal);
    REQUIRE(layout.entries[1].legal);
    REQUIRE_FALSE(layout.entries[2].legal);
    REQUIRE_FALSE(layout.entries[3].legal);

    // The whole hand is still mapped. The terminal's `choices` covered the two
    // legal cards only; a display slot that cannot be played still has to know
    // which card it is drawing, or it cannot draw it.
    REQUIRE(layout.positions == std::vector<std::size_t>{ 1, 3, 2, 0 });

    requireEverySlotLeadsBackToItsCard(layout, Hand);
}

TEST_CASE("A single legal card leaves the rest mapped", "[hand-layout]")
{
    const std::vector<Card> legal{ HeartsKing };

    const HandLayout layout = layOutHand(Hand, legal, std::nullopt);

    REQUIRE(layout.entries[2].card == HeartsKing);
    REQUIRE(layout.entries[2].legal);
    REQUIRE(layout.positions[2] == 2);

    requireEverySlotLeadsBackToItsCard(layout, Hand);
}

TEST_CASE("An empty legal set means nothing is being chosen", "[hand-layout]")
{
    // The bidding screen, and every hand drawn between turns: the cards are
    // shown so the player can judge a bid, but none is being picked, so nothing
    // is greyed. This is exactly what GameSnapshot asks for every frame.
    const HandLayout layout = layOutHand(Hand, {}, std::nullopt);

    REQUIRE(layout.entries.size() == Hand.size());

    for(const HandEntry& entry : layout.entries)
        REQUIRE(entry.legal);

    // Still fully mapped - "nothing is being chosen" is about legality, not
    // about whether the slots know which cards they hold.
    requireEverySlotLeadsBackToItsCard(layout, Hand);
}

TEST_CASE("An empty hand lays out to nothing at all", "[hand-layout]")
{
    const HandLayout layout = layOutHand({}, {}, std::nullopt);

    REQUIRE(layout.entries.empty());
    REQUIRE(layout.positions.empty());
}
