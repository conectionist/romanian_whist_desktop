#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "engine/GameAbandoned.h"
#include "engine/GameBridge.h"
#include "engine/QtMoveProvider.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/BetContext.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/PlayContext.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace romanian_whist;

// Headless: no QThread, no event loop, no engine. The provider parks on a
// condition variable and submit() answers it on another thread, which is
// exactly the shape a click has - so everything here is testable with two
// std::threads and no Qt machinery at all.
//
// The bridge is real but inert. Its signals have no connections, so raising a
// prompt through it is a no-op that returns immediately, which is all these
// tests need from it. A GameBridge with GUI-thread affinity and a queued
// connection is phase A4's problem.

namespace
{
// Only ever used to prove a NEGATIVE - that a dropped submission did not answer
// something. Nothing waits on this to make progress; see waitForPrompt below
// for why that distinction matters.
constexpr auto kSettle = std::chrono::milliseconds(50);

// Blocks until the provider has raised a prompt NEWER than `after`.
//
// A fixed sleep cannot do this job. The provider bumps its generation on the
// worker thread, and submit() names a generation - so a submit() that runs
// before the worker gets there is dropped as stale and the provider parks
// forever, which surfaces as a HANG rather than a failure. That is a coin flip
// on a loaded CI runner and it would be an ugly one to diagnose.
//
// The prompt signals are emitted with no receiver context, so the lambda runs
// directly on the worker thread as the prompt is raised, which makes this an
// exact synchronisation point rather than an estimate.
unsigned int waitForPrompt(const std::atomic<unsigned int>& promptId, unsigned int after)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while(promptId.load() <= after)
    {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return promptId.load();
}

const Card SpadesAce(Rank::Ace, Suit::Spades);
const Card HeartsKing(Rank::King, Suit::Hearts);
const Card DiamondsSeven(Rank::Seven, Suit::Diamonds);

const std::vector<Card> Hand{ SpadesAce, HeartsKing, DiamondsSeven };

// Runs playCard() on its own thread and records what came back, so the test
// thread stands in for the GUI and can submit while the provider is parked.
class ParkedPlay
{
public:
    // Watches the bridge so the test can wait for a prompt to actually exist
    // rather than guess at how long raising one takes.
    ParkedPlay(GameBridge& bridge, QtMoveProvider& _provider, const std::vector<Card>& hand)
        : provider(_provider), context{ hand, played, std::nullopt, std::nullopt, 0, 0 }
    {
        QObject::connect(&bridge, &GameBridge::cardRequested,
                         [this](PlayPrompt prompt)
                         {
                             // ORDER MATTERS, and getting it backwards is a data
                             // race ThreadSanitizer catches: `raised` is written
                             // here on the worker thread and read on the test
                             // thread, with nothing but this atomic between them.
                             //
                             // Storing the id LAST makes it a release, and the
                             // acquiring load in waitForPrompt() the other half -
                             // so a reader that has seen the id has also seen
                             // everything written before it. Store the id first
                             // and the reader is free to observe a half-written
                             // prompt.
                             const unsigned int id = prompt.promptId;
                             raised = std::move(prompt);
                             lastPrompt.store(id);
                         });

        worker = std::thread([this]
        {
            try
            {
                result = provider.playCard(context);
            }
            catch(const GameAbandoned&)
            {
                threw = true;
            }

            done = true;
        });
    }

    ~ParkedPlay()
    {
        if(worker.joinable())
            worker.join();
    }

    void join() { worker.join(); }

    // Waits for the prompt after `after` to be raised, and returns its id.
    unsigned int awaitPrompt(unsigned int after = 0) { return waitForPrompt(lastPrompt, after); }

    // Only for proving a negative - that nothing answered when it should not
    // have. Never used to wait for progress.
    void settle() const { std::this_thread::sleep_for(kSettle); }

    std::atomic<bool> done{false};
    std::atomic<bool> threw{false};
    std::atomic<unsigned int> lastPrompt{0};
    std::optional<std::size_t> result;
    PlayPrompt raised;

private:
    QtMoveProvider& provider;
    std::vector<Card> played;
    PlayContext context;
    std::thread worker;
};
}

TEST_CASE("A submission for the live prompt is accepted", "[provider]")
{
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    ParkedPlay parked(bridge, provider, Hand);
    const unsigned int prompt = parked.awaitPrompt();

    REQUIRE(prompt == 1);            // the first prompt this provider ever raised
    REQUIRE_FALSE(parked.done);

    provider.submit(prompt, 0);
    parked.join();

    REQUIRE(parked.done);
    REQUIRE(parked.result.has_value());
    REQUIRE(*parked.result == 0);
}

TEST_CASE("A submission for a stale generation is dropped", "[provider]")
{
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    ParkedPlay parked(bridge, provider, Hand);
    const unsigned int prompt = parked.awaitPrompt();

    // Answering a generation that is not the live one - a click from a widget
    // drawn for an older prompt.
    provider.submit(prompt + 6, 0);
    parked.settle();

    REQUIRE_FALSE(parked.done);

    // Still parked, and still answerable by the right generation.
    provider.submit(prompt, 1);
    parked.join();

    REQUIRE(*parked.result == 1);
}

TEST_CASE("A second submission for the same prompt is dropped", "[provider]")
{
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    ParkedPlay parked(bridge, provider, Hand);
    const unsigned int prompt = parked.awaitPrompt();

    provider.submit(prompt, 2);
    provider.submit(prompt, 0);   // a double click; must not overwrite the first
    parked.join();

    REQUIRE(*parked.result == 2);
}

TEST_CASE("A submission with nothing parked does not answer the next prompt", "[provider]")
{
    // THE bug the generation gate exists for. Without it, a stray click during a
    // bot's turn sets the answer, and the next prompt returns from cv.wait()
    // immediately with a choice made for a different hand - which reaches a
    // player as "it played a card I did not click".
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    // Nothing is parked: no prompt has been raised, so the generation is 0 and
    // there is no live prompt for this to answer.
    provider.submit(1, 2);

    ParkedPlay parked(bridge, provider, Hand);
    const unsigned int prompt = parked.awaitPrompt();

    // The prompt is raised. If the stray click had armed it, the provider would
    // have returned already - so this is the assertion, and the settle only
    // gives a broken implementation time to prove it is broken.
    parked.settle();
    REQUIRE_FALSE(parked.done);

    provider.submit(prompt, 0);
    parked.join();

    REQUIRE(*parked.result == 0);
}

TEST_CASE("An illegal choice re-prompts rather than reaching the engine", "[provider]")
{
    // Following spades: only the ace is legal. The prompt marks the rest
    // unplayable and the widget is meant to refuse them; this is what runs when
    // it does not. Handing the engine an illegal play throws out of run() and
    // ends the game.
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    std::vector<Card> played{ Card(Rank::Two, Suit::Spades) };
    PlayContext context{ Hand, played, std::nullopt, Suit::Spades, 0, 0 };

    std::atomic<unsigned int> lastPrompt{0};
    QObject::connect(&bridge, &GameBridge::cardRequested,
                     [&lastPrompt](PlayPrompt prompt){ lastPrompt = prompt.promptId; });

    std::atomic<bool> done{false};
    std::optional<std::size_t> result;

    std::thread worker([&]
    {
        result = provider.playCard(context);
        done = true;
    });

    const unsigned int first = waitForPrompt(lastPrompt, 0);

    // Hand position 1 is the king of hearts - legal only if spades cannot be
    // followed, and this hand can follow.
    provider.submit(first, 1);

    // Re-prompted rather than returned, which is the assertion.
    const unsigned int second = waitForPrompt(lastPrompt, first);
    REQUIRE_FALSE(done);

    // The ace of spades is hand position 0.
    provider.submit(second, 0);
    worker.join();

    REQUIRE(*result == 0);
}

TEST_CASE("A choice past the end of the hand re-prompts", "[provider]")
{
    // handIndex is the only sanctioned way to produce this number, and a widget
    // indexing it with a stale display slot produces one out of range.
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    ParkedPlay parked(bridge, provider, Hand);
    const unsigned int first = parked.awaitPrompt();

    provider.submit(first, 99);

    // Re-prompted rather than returned, so a NEWER generation appears.
    const unsigned int second = parked.awaitPrompt(first);
    REQUIRE(second > first);
    REQUIRE_FALSE(parked.done);

    provider.submit(second, 0);
    parked.join();

    REQUIRE(*parked.result == 0);
}

TEST_CASE("abandon() throws out of a parked provider", "[provider]")
{
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    ParkedPlay parked(bridge, provider, Hand);
    parked.awaitPrompt();

    REQUIRE_FALSE(parked.done);

    provider.abandon();
    parked.join();

    REQUIRE(parked.threw);
    REQUIRE_FALSE(parked.result.has_value());
}

TEST_CASE("abandon() before the provider parks still throws, rather than hanging", "[provider]")
{
    // A window closed during setup, or between turns. abandon() only latches a
    // flag, so the next wait sees it already set and never blocks - if it did
    // not, this test would hang rather than fail, which is why it is worth
    // having.
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    provider.abandon();

    ParkedPlay parked(bridge, provider, Hand);
    parked.join();

    REQUIRE(parked.threw);
}

TEST_CASE("A bet outside the legal range re-prompts", "[provider]")
{
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    // Bidding last, with 2 forbidden: the two rules GameEngine::isBetLegal()
    // applies, restated here because the provider holds no engine reference.
    BetContext context{ Hand, std::nullopt, false, 2u, RoundType::Normal };

    std::atomic<unsigned int> lastPrompt{0};
    QObject::connect(&bridge, &GameBridge::betRequested,
                     [&lastPrompt](BetPrompt prompt){ lastPrompt = prompt.promptId; });

    std::atomic<bool> done{false};
    unsigned int result = 0;

    std::thread worker([&]
    {
        result = provider.makeBet(context);
        done = true;
    });

    const unsigned int first = waitForPrompt(lastPrompt, 0);
    provider.submit(first, 9);      // more tricks than there are cards

    const unsigned int second = waitForPrompt(lastPrompt, first);
    REQUIRE_FALSE(done);
    provider.submit(second, 2);     // the forbidden bet

    const unsigned int third = waitForPrompt(lastPrompt, second);
    REQUIRE_FALSE(done);
    provider.submit(third, 1);

    worker.join();

    REQUIRE(result == 1);
}

TEST_CASE("A normal round's prompt carries the cards and the way back", "[provider]")
{
    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    ParkedPlay parked(bridge, provider, Hand);
    parked.awaitPrompt();

    const PlayPrompt& seen = parked.raised;

    REQUIRE_FALSE(seen.blind);
    REQUIRE(seen.entries.size() == Hand.size());
    REQUIRE(seen.handIndex.size() == Hand.size());

    // Every display slot names the card it is drawing, over the whole hand.
    for(std::size_t i = 0 ; i < seen.entries.size() ; i++)
        REQUIRE(Hand[seen.handIndex[i]] == seen.entries[i].card);

    provider.submit(seen.promptId, seen.handIndex[0]);
    parked.join();

    REQUIRE(*parked.result == seen.handIndex[0]);
}

TEST_CASE("A blind round's prompt carries no cards but still carries the way back", "[provider]")
{
    // Forehead and Hidden: the engine passes the real hand because it has no
    // other one to give, and the prompt must not send it on. What it DOES send
    // is handIndex, which names positions rather than cards - so it leaks
    // nothing, the click handler needs no special case, and its size is how many
    // face-down backs to draw.
    //
    // The round type reaches the provider only through the bridge's cache,
    // because PlayContext does not carry one. So this needs a real engine parked
    // in a blind round: all1GamesAreForehead makes round 1 a Forehead round,
    // which is far cheaper than playing 24 rounds to reach the appended ones.
    GameEngine engine;

    GameSetup setup;
    setup.seats.push_back(SeatSetup{ "Ana",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Bogdan",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });
    setup.structure = GameStructure::S_181;
    setup.all1GamesAreForehead = true;
    setup.shuffleSeed = 42u;

    engine.start(std::move(setup));

    REQUIRE(engine.getCurrentRoundType() == RoundType::Forehead);

    GameBridge bridge(Seat{0}, Pacing{});
    QtMoveProvider provider(&bridge, Seat{0});

    // What the engine does on its way into a round, and the only route by which
    // the round type reaches the provider.
    bridge.onRoundStarted(engine);

    // One card, as every Forehead and Hidden round has.
    const std::vector<Card> oneCard{ SpadesAce };

    ParkedPlay parked(bridge, provider, oneCard);
    parked.awaitPrompt();

    const PlayPrompt& seen = parked.raised;

    REQUIRE(seen.blind);

    // The card is NOT sent...
    REQUIRE(seen.entries.empty());

    // ...but the way back is, so the widget can draw a back and submit a click.
    REQUIRE(seen.handIndex.size() == 1);
    REQUIRE(seen.handIndex[0] == 0);

    provider.submit(seen.promptId, seen.handIndex[0]);
    parked.join();

    REQUIRE(*parked.result == 0);
}
