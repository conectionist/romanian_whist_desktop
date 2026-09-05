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
    // callback that ends in one. onRoundScored() paces for the trick dwell,
    // which is 30 seconds here.
    //
    // ON onRoundScored AND NOT onRoundComplete, and that is not interchangeable:
    // the dwell moved between the two in A5, once the log showed it was landing
    // on the blank next-round frame instead of the round result. Driving this
    // through onRoundComplete now would pace for nothing and pass in about a
    // microsecond, testing that cancelPacing() does not crash.
    GameEngine engine;
    engine.start(buildSetup());

    std::thread worker([&]
    {
        try
        {
            bridge.onRoundScored(engine);
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

TEST_CASE("pace() actually waits when nothing has cancelled it", "[bridge]")
{
    // The counterweight to the two tests around it, and it was missing: both of
    // them assert that pace() comes back promptly and does not throw, which a
    // pace() that never waited at all would satisfy perfectly.
    //
    // That gap is worth closing rather than shrugging at. Pacing is the whole
    // difference between a watchable game and cards appearing all at once, and
    // nothing else in the suite would notice it going away.
    //
    // TWO DWELLS, NOT ONE, and this is the part a lower bound alone does not
    // say: "it waited 100ms" is also satisfied by a pace() that ignores its
    // argument and sleeps a fixed amount. Running the same callback at 200ms and
    // at 0 and requiring the two land on opposite sides of the same threshold is
    // what pins that the argument is READ, which is what every other pacing knob
    // in this class depends on.
    //
    // A short dwell and a generous split: the assertion is "it waited", not "it
    // waited precisely", and a loaded machine must not be able to fail this.
    GameEngine engine;
    engine.start(buildSetup());

    GameBridge paced(std::nullopt, Pacing{ std::chrono::milliseconds(0),
                                           std::chrono::milliseconds(0),
                                           std::chrono::milliseconds(200) });

    auto started = std::chrono::steady_clock::now();
    paced.onRoundScored(engine);
    const auto waited = std::chrono::steady_clock::now() - started;

    REQUIRE(waited >= std::chrono::milliseconds(100));

    GameBridge instantly(std::nullopt, instant());

    started = std::chrono::steady_clock::now();
    instantly.onRoundScored(engine);
    const auto immediate = std::chrono::steady_clock::now() - started;

    // The same callback, the same engine, the same publish - only the dwell
    // differs, so this is the dwell and nothing else.
    REQUIRE(immediate < std::chrono::milliseconds(100));
}

TEST_CASE("A bot is given a beat before it is asked for a card", "[bridge]")
{
    // THE BEAT THAT MAKES THE SEAT ON TURN VISIBLE. onCardRequested publishes
    // the frame that names who is thinking, and onCardPlayed supersedes it
    // microseconds later - measured under a millisecond - so without this wait
    // that frame is never seen and cards teleport onto the table.
    //
    // Nothing asserted it before: the suite's pacing tests all drove
    // onRoundScored, so deleting this beat left every one of them green.
    GameBridge bridge(std::nullopt, Pacing{ std::chrono::milliseconds(0),
                                            std::chrono::milliseconds(200),
                                            std::chrono::milliseconds(0) });

    GameEngine engine;
    engine.start(buildSetup());

    const auto started = std::chrono::steady_clock::now();
    bridge.onCardRequested(engine, Seat{ 0u });
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(elapsed >= std::chrono::milliseconds(100));
}

TEST_CASE("The human is not made to wait before being asked for a card", "[bridge]")
{
    // Their own deliberation is the pause. Pacing before the prompt would only
    // delay the prompt, which is the one thing a player is waiting for.
    GameBridge bridge(Seat{ 0u }, Pacing{ std::chrono::seconds(30),
                                          std::chrono::seconds(30),
                                          std::chrono::seconds(30) });

    GameEngine engine;
    engine.start(buildSetup());

    const auto started = std::chrono::steady_clock::now();
    bridge.onCardRequested(engine, Seat{ 0u });
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(elapsed < std::chrono::seconds(5));
}

TEST_CASE("The frames after a card and after a round take no dwell of their own", "[bridge]")
{
    // BOTH HALVES OF WHERE THE PACING IS NOT, as assertions, because both were
    // reached by measuring a real game's log and both are invisible to every
    // other test in this file.
    //
    // onCardPlayed: the beat belongs to onCardRequested above. Pacing here as
    // well spends two per card, and the second one dwells on a table that has
    // not changed since the first - the seats run back to back inside
    // playTrick(), so the very next frame is the next seat being asked.
    //
    // onRoundComplete: the dwell belongs to onRoundScored, the last callback at
    // which the engine still names the round just scored. Paced here it lands on
    // an advanced-index frame instead - a next round with no trump, no bets and
    // nothing played, which is nothing anyone needs to read.
    //
    // Thirty seconds apiece, so a wait that came back would not be mistaken for
    // a slow machine.
    GameBridge bridge(std::nullopt, Pacing{ std::chrono::seconds(30),
                                            std::chrono::seconds(30),
                                            std::chrono::seconds(30) });

    GameEngine engine;
    engine.start(buildSetup());

    const auto started = std::chrono::steady_clock::now();

    bridge.onCardPlayed(engine, Seat{ 0u }, Card(Rank::Ace, Suit::Spades));
    bridge.onRoundComplete(engine);

    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(elapsed < std::chrono::seconds(5));
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

    bridge.onRoundScored(engine);
    bridge.onRoundScored(engine);
    bridge.onRoundScored(engine);

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

TEST_CASE("Every snapshot names the seat on turn, or nobody", "[bridge]")
{
    // The other half of what refreshFromEngine() leaves to the caller, and the
    // reason the bridge keeps no activeSeat of its own: GameEngine models whose
    // turn it is, with exactly the meaning the snapshot's -1 has, so publish()
    // asks rather than tracks.
    //
    // Which makes this the test of that agreement, across a whole real schedule
    // rather than a hand-written call order. Driving the callbacks by hand
    // cannot check it at all - the engine's own activeSeat is empty the whole
    // time - and the agreement is the entire point.
    //
    // turnChanged() is not a substitute for the field, and this is why: it
    // announces a turn STARTING, and half the frames below are ones where
    // nobody is on turn, which that signal has no way to express.
    GameBridge bridge(std::nullopt, instant());

    std::shared_ptr<const GameSnapshot> latest;
    QObject::connect(&bridge, &GameBridge::stateChanged,
                     [&latest](std::shared_ptr<const GameSnapshot> snapshot)
                     { latest = std::move(snapshot); });

    // Registered AFTER the bridge, which is what makes "the latest snapshot" this
    // callback's own: addObserver() pushes back and every notify* walks the list
    // front to back, so the bridge has already published by the time this runs.
    //
    // Records rather than asserts. A REQUIRE here throws through the engine's
    // dispatch guard mid-walk, which is not a thing to do for a failure that
    // reads just as well after run() returns.
    class ActiveSeatChecker : public IGameObserver
    {
    public:
        explicit ActiveSeatChecker(const std::shared_ptr<const GameSnapshot>& _latest)
            : latest(_latest)
        {}

        void onGameStarted(const GameEngine& e) override { check(e); }
        void onRoundStarted(const GameEngine& e) override { check(e); }
        void onBetRequested(const GameEngine& e, Seat) override { check(e); }
        void onBetPlaced(const GameEngine& e, Seat, unsigned int) override { check(e); }
        void onBettingComplete(const GameEngine& e) override { check(e); }
        void onTrickStarted(const GameEngine& e, unsigned int, Seat) override { check(e); }
        void onCardRequested(const GameEngine& e, Seat) override { check(e); }
        void onCardPlayed(const GameEngine& e, Seat, const Card&) override { check(e); }
        void onTrickWon(const GameEngine& e, Seat, unsigned int) override { check(e); }
        void onRoundScored(const GameEngine& e) override { check(e); }
        void onRoundComplete(const GameEngine& e) override { check(e); }
        void onGameOver(const GameEngine& e) override { check(e); }

        // Deliberately not onGameStopped: it is the one callback that publishes
        // nothing, so the latest snapshot there belongs to some earlier frame. A
        // game run to the end never reaches it anyway.

        unsigned int frames = 0;
        unsigned int disagreed = 0;
        bool sawSeat = false;
        bool sawNobody = false;

    private:
        void check(const GameEngine& engine)
        {
            const std::optional<Seat> active = engine.getActiveSeat();
            const int expected = active ? static_cast<int>(active->index) : -1;

            ++frames;

            if(!latest || latest->activeSeat != expected)
                ++disagreed;

            if(expected == -1)
                sawNobody = true;
            else
                sawSeat = true;
        }

        const std::shared_ptr<const GameSnapshot>& latest;
    };

    ActiveSeatChecker checker(latest);

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.addObserver(&checker);
    engine.start(buildSetup());
    engine.run();

    REQUIRE(checker.frames > 0);
    REQUIRE(checker.disagreed == 0);

    // Both halves, so the test cannot pass by saying "nobody" to everything -
    // which is exactly what a publish() that stopped filling the field would do.
    REQUIRE(checker.sawSeat);
    REQUIRE(checker.sawNobody);
}
