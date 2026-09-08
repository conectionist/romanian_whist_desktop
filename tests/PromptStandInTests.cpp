#include <catch2/catch_test_macros.hpp>

#include "ui/PromptStandIn.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/RoundType.h>

#include <cstddef>
#include <vector>

using namespace romanian_whist;
using promptstandin::chooseSlot;

// The stand-in that unparks the engine until C3 builds the hand that will. It is
// temporary and these tests go with it - but while it is what answers every card
// in a run of the client, its one rule decides whether a game plays out or
// loops, so it is worth the two assertions.
//
// HALF THE FILE LEFT IN C2. The bet cases moved to BidStripTests, beside the
// widget that answers a bid now, and the function they covered moved to
// fixtures::lowestLegalBid() - see tests/GameFixtures.h for why it did not stay
// in src/.
//
// No thread, no engine, no widget: this is the part of C1 with a right answer,
// which is the reason it is a free function rather than logic inside a slot.

namespace
{
const Card SpadesAce(Rank::Ace, Suit::Spades);
const Card HeartsKing(Rank::King, Suit::Hearts);
const Card DiamondsSeven(Rank::Seven, Suit::Diamonds);

// `legal` is the flag that means something only on a prompt - on a snapshot it
// is always true, which is why a snapshot's hand could never be the thing a
// player clicks.
PlayPrompt playPrompt(std::vector<bool> legality)
{
    PlayPrompt prompt;
    prompt.promptId = 7;

    const std::vector<Card> cards{ SpadesAce, HeartsKing, DiamondsSeven };

    for(std::size_t i = 0 ; i < legality.size() ; i++)
    {
        prompt.entries.push_back(HandEntry{ cards[i % cards.size()], legality[i] });

        // Deliberately NOT the identity. Display order is not deal order, and a
        // chooseSlot() that returned a hand position rather than a display slot
        // would agree with the identity and with nothing else.
        prompt.handIndex.push_back(legality.size() - 1 - i);
    }

    return prompt;
}
}

TEST_CASE("The stand-in picks the first LEGAL slot, not the first slot", "[standin]")
{
    // Slot 0 is the answer to "which card is on the left", and the answer to
    // "which card may be played" only by coincidence. Submitting an illegal one
    // is caught by playCard()'s own guard and re-prompted, so this failing is a
    // loop rather than an illegal play - and a loop is what the stand-in exists
    // to rule out of C3's and C4's failures.
    const PlayPrompt prompt = playPrompt({ false, false, true });

    REQUIRE(chooseSlot(prompt) == 2);
}

TEST_CASE("A blind prompt has one slot and the stand-in picks it", "[standin]")
{
    // Forehead and Hidden: `entries` is empty because the player is not entitled
    // to see the card, `handIndex` carries the way back regardless, and there is
    // one card. A stand-in that walked `entries` looking for a legal flag would
    // find no slots at all here - which is the shape of the bug section 8 says
    // stalls the last two rounds of every default game.
    PlayPrompt prompt;
    prompt.promptId = 11;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    REQUIRE(prompt.entries.empty());
    REQUIRE(chooseSlot(prompt) == 0);
}
