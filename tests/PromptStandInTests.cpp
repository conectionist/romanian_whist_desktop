#include <catch2/catch_test_macros.hpp>

#include "ui/PromptStandIn.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/RoundType.h>

#include <cstddef>
#include <optional>
#include <vector>

using namespace romanian_whist;
using promptstandin::chooseBet;
using promptstandin::chooseSlot;

// The stand-in that unparks the engine until C2 and C3 build the widgets that
// will. It is temporary and these tests go with it - but while it is what
// answers every prompt in a run of the client, its two rules are what decide
// whether a game plays out or loops, so they are worth the four assertions.
//
// No thread, no engine, no widget: this is the part of C1 with a right answer,
// which is the reason it is a pair of free functions rather than logic inside a
// slot.

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

TEST_CASE("The stand-in bids zero when zero is allowed", "[standin]")
{
    BetPrompt prompt;
    prompt.handSize = 8;
    prompt.forbiddenBet = 3;

    REQUIRE(chooseBet(prompt) == 0);
}

TEST_CASE("The stand-in bids one when zero is the forbidden bet", "[standin]")
{
    // The whole reason chooseBet() is a function rather than a literal 0. The
    // engine forbids exactly one value and only for the final bidder, and a
    // stand-in that ignored it would not crash: QtMoveProvider::makeBet() checks
    // the same two rules and RE-PROMPTS, so the visible symptom would be the
    // client bidding forever in some rounds and not in others.
    BetPrompt prompt;
    prompt.handSize = 8;
    prompt.forbiddenBet = 0;

    REQUIRE(chooseBet(prompt) == 1);
}

TEST_CASE("A bet with nothing forbidden is still the lowest one", "[standin]")
{
    // Every bidder but the last, and the last one once the bids already exceed
    // the trick count - getForbiddenBet() is disengaged in both.
    BetPrompt prompt;
    prompt.handSize = 1;
    prompt.forbiddenBet = std::nullopt;
    prompt.roundType = RoundType::Forehead;

    REQUIRE(chooseBet(prompt) == 0);
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
