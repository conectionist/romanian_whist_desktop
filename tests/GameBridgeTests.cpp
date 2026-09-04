#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "engine/GameBridge.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace romanian_whist;

namespace
{
GameSetup buildSetup(std::optional<std::uint32_t> seed = 42u)
{
    GameSetup setup;

    setup.seats.push_back(SeatSetup{ "Ana",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Bogdan",
        std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>()) });

    setup.structure = GameStructure::S_181;
    setup.shuffleSeed = seed;

    return setup;
}

// Pacing fast enough that a whole game runs in a test, since every publish is
// followed by one of these waits.
Pacing instant()
{
    return Pacing{ std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0) };
}
}

TEST_CASE("pace() returns early on cancelPacing(), and does not throw", "[bridge]")
{
    // ONE assertion, and section 6's entire shutdown story rests on it.
    //
    // The provider has no choice but to throw - IMoveProvider has no
    // non-throwing way to say "abandon". pace() HAS a choice and takes the other
    // one: returning puts the game thread back in run() within microseconds,
    // where it reaches its next boundary, reads the stop flag, and ends cleanly
    // through onGameStopped().
    //
    // If it threw instead, the clean path would almost never run. A spectator
    // game spends nearly all its wall-clock time asleep in here - 250ms after
    // every play, 1200ms after every trick - so a cancel would win the race
    // against requestStop() essentially every time, and phase A's shutdown test
    // would be verifying the exception path while claiming to verify the clean
    // one.
    GameBridge bridge(std::nullopt, Pacing{ std::chrono::milliseconds(30000),
                                            std::chrono::milliseconds(30000),
                                            std::chrono::milliseconds(30000) });

    std::atomic<bool> returnedNormally{false};
    std::atomic<bool> threw{false};

    const auto started = std::chrono::steady_clock::now();

    // pace() is private, so it is exercised the way the engine does - through a
    // callback that ends in one. onRoundComplete() paces for the trick dwell,
    // which is 30 seconds here.
    GameEngine engine;
    engine.start(buildSetup());

    std::thread worker([&]
    {
        try
        {
            bridge.onRoundComplete(engine);
            returnedNormally = true;
        }
        catch(...)
        {
            threw = true;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bridge.cancelPacing();
    worker.join();

    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(returnedNormally);
    REQUIRE_FALSE(threw);

    // It came back on the cancel rather than waiting out the 30-second dwell.
    REQUIRE(elapsed < std::chrono::seconds(20));
}

TEST_CASE("cancelPacing() latches, so later waits do not block either", "[bridge]")
{
    // A close cancels once, but the game thread may pass through several more
    // paced callbacks before it reaches a boundary at which the stop flag is
    // read. Every one of those has to fall straight through, or the close waits
    // out a dwell per callback.
    GameBridge bridge(std::nullopt, Pacing{ std::chrono::milliseconds(30000),
                                            std::chrono::milliseconds(30000),
                                            std::chrono::milliseconds(30000) });

    GameEngine engine;
    engine.start(buildSetup());

    bridge.cancelPacing();

    const auto started = std::chrono::steady_clock::now();

    bridge.onRoundComplete(engine);
    bridge.onRoundComplete(engine);
    bridge.onRoundComplete(engine);

    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(elapsed < std::chrono::seconds(5));
}

TEST_CASE("Every publishing callback emits a snapshot the GUI thread owns", "[bridge]")
{
    // Section 3's cardinal rule, as a test: what crosses the boundary is a value,
    // never a reference into the engine. The shared_ptr is to a CONST snapshot,
    // so two threads cannot hold the same mutable one.
    GameBridge bridge(std::nullopt, instant());

    std::vector<std::shared_ptr<const GameSnapshot>> seen;
    QObject::connect(&bridge, &GameBridge::stateChanged,
                     [&seen](std::shared_ptr<const GameSnapshot> snapshot)
                     { seen.push_back(std::move(snapshot)); });

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.start(buildSetup());
    engine.run();

    REQUIRE_FALSE(seen.empty());

    for(const auto& snapshot : seen)
    {
        REQUIRE(snapshot != nullptr);
        REQUIRE(snapshot->seats.size() == 2);
    }

    // The first one is onGameStarted's, which fires before the deal.
    REQUIRE(seen.front()->roundNumber == 1);
    REQUIRE_FALSE(seen.front()->hasTrump);

    // A spectator game names no human seat, so no hand is ever filled.
    for(const auto& snapshot : seen)
        REQUIRE(snapshot->humanSeat == -1);
}

TEST_CASE("A game that ends on its own reports winners, not a first row", "[bridge]")
{
    // getWinners() rather than standings.front(): a drawn game has more than one
    // winner, and reading the first row quietly names one of them. That bug
    // shipped once already, in the terminal's 2.1.0.
    GameBridge bridge(std::nullopt, instant());

    std::vector<Standing> standings;
    std::vector<Standing> winners;
    int gameOverCount = 0;
    int stoppedCount = 0;

    QObject::connect(&bridge, &GameBridge::gameOver,
                     [&](std::vector<Standing> s, std::vector<Standing> w)
                     {
                         standings = std::move(s);
                         winners = std::move(w);
                         gameOverCount++;
                     });

    QObject::connect(&bridge, &GameBridge::gameStopped, [&]{ stoppedCount++; });

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.start(buildSetup());
    engine.run();

    REQUIRE(gameOverCount == 1);

    // onGameStopped fires INSTEAD of onGameOver, never alongside it.
    REQUIRE(stoppedCount == 0);

    REQUIRE(standings.size() == 2);
    REQUIRE_FALSE(winners.empty());

    // Whoever won is on the top score, and holds first place.
    for(const Standing& winner : winners)
    {
        REQUIRE(winner.place == 1);
        REQUIRE(winner.score == standings.front().score);
    }
}

TEST_CASE("A stopped game reports gameStopped and never gameOver", "[bridge]")
{
    // What a window closed mid-game produces. A client that only handles
    // onGameOver() is silent on the exact path it was written for.
    GameBridge bridge(std::nullopt, instant());

    int gameOverCount = 0;
    int stoppedCount = 0;

    QObject::connect(&bridge, &GameBridge::gameOver,
                     [&](std::vector<Standing>, std::vector<Standing>){ gameOverCount++; });
    QObject::connect(&bridge, &GameBridge::gameStopped, [&]{ stoppedCount++; });

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.start(buildSetup());

    // Requested before run(), so it is honoured at the first boundary rather
    // than racing the schedule.
    engine.requestStop();
    engine.run();

    REQUIRE(stoppedCount == 1);
    REQUIRE(gameOverCount == 0);
}

TEST_CASE("The round type is cached for the provider, which cannot read it itself", "[bridge]")
{
    // PlayContext carries no roundType the way BetContext does, so this cache is
    // the only route by which a blind round reaches QtMoveProvider. It cannot be
    // inferred from the hand size either - a Normal 1-trick round looks
    // identical to a Forehead one.
    GameBridge bridge(std::nullopt, instant());

    // Fresh bridges report Normal, which is the right default for a game that
    // has not started.
    REQUIRE(bridge.currentRoundType() == RoundType::Normal);

    GameEngine engine;

    GameSetup setup = buildSetup();
    setup.all1GamesAreForehead = true;
    engine.start(std::move(setup));

    bridge.onRoundStarted(engine);

    REQUIRE(bridge.currentRoundType() == RoundType::Forehead);
    REQUIRE(engine.getCurrentRoundTrickCount() == 1);
}
