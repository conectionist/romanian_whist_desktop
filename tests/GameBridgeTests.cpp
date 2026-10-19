#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameFixtures.h"

#include "engine/GameBridge.h"
#include "table/TrickItem.h"

#include <romanian_whist/GameEngine.h>

#include <algorithm>
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
// Both come from tests/GameFixtures.h now, which A6 needed a four-seat variant
// of. Kept as thin local names so the call sites below - including the one that
// passes a different SEED - read exactly as they did.
Pacing instant()
{
    return fixtures::instant();
}

GameSetup buildSetup(std::optional<std::uint32_t> seed = 42u)
{
    return fixtures::buildSetup(2, seed);
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

TEST_CASE("setPaused() holds pace() past its delay, and resuming releases it", "[bridge]")
{
    // D1's pause, and the first INDEFINITE wait in the client. Everything above
    // this line waits for a bounded time and comes back on its own; this one
    // comes back only when somebody says so, which is what section 15.3 warns
    // is a different kind of object.
    //
    // AN INSTANT PACING, deliberately. If the dwell were long the case would
    // pass whether the pause worked or not - the wait it measures would be the
    // beat. At zero, every millisecond spent inside onRoundScored() is the pause
    // and nothing else.
    GameEngine engine;
    engine.start(buildSetup());

    GameBridge bridge(std::nullopt, instant());
    bridge.setPaused(true);

    std::atomic<bool> returned{false};

    std::thread worker([&]
    {
        bridge.onRoundScored(engine);
        returned = true;
    });

    // OBSERVED, THEN ASSERTED AFTER THE JOIN, and not asserted here. A failing
    // REQUIRE unwinds, and unwinding past a std::thread that has not been joined
    // is std::terminate - so the mutation this case exists to catch would abort
    // the run instead of failing it, taking every later case with it and
    // printing an abort where a failure belongs. C4 found the same shape from
    // the other direction: a mutation that crashes proves nothing about the test.
    //
    // Long enough that a pace() which ignored the flag would be finished many
    // times over - its dwell is zero.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const bool heldWhilePaused = !returned;

    const auto resumed = std::chrono::steady_clock::now();
    bridge.setPaused(false);
    worker.join();

    REQUIRE(heldWhilePaused);
    REQUIRE(returned);

    // It came back ON the resume rather than at some later moment of its own -
    // which is what says the resume is the thing that released it.
    REQUIRE(std::chrono::steady_clock::now() - resumed < std::chrono::seconds(5));
}

TEST_CASE("A pace entered while paused takes no dwell after the release", "[bridge]")
{
    // D2's REVIEW FINDING, and the reason this is the one pacing case that must
    // NOT use instant(): at a zero dwell it passes whether the fix is there or
    // not, which is exactly why the MainWindow case above it - running at
    // FastestSpeed - could not see the bug. A 300ms dwell is what makes the
    // stacked wait measurable.
    //
    // What it pins: the hold and the dwell are ONE beat. onRoundScored() holds
    // the game and then paces it, and before the guard in pace() the release had
    // to wait the dwell out before anything moved - up to 2.4s of a Continue
    // click doing nothing at the bottom of the speed slider.
    GameEngine engine;
    engine.start(buildSetup());

    GameBridge bridge(std::nullopt, Pacing{ std::chrono::milliseconds(0),
                                            std::chrono::milliseconds(0),
                                            std::chrono::milliseconds(300) });
    bridge.setPaused(true);

    std::atomic<bool> returned{false};

    std::thread worker([&]
    {
        bridge.onRoundScored(engine);
        returned = true;
    });

    // OBSERVED, THEN ASSERTED AFTER THE JOIN, for the reason the case above
    // records: a REQUIRE that unwinds past an unjoined std::thread is
    // std::terminate, so the mutation this exists to catch would abort the run
    // rather than fail one case.
    //
    // Comfortably inside the dwell, so a bridge that skipped the HOLD instead of
    // the dwell fails here rather than passing on a technicality.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool heldWhilePaused = !returned;

    const auto resumed = std::chrono::steady_clock::now();
    bridge.setPaused(false);
    worker.join();
    const auto afterRelease = std::chrono::steady_clock::now() - resumed;

    REQUIRE(heldWhilePaused);

    // THE FINDING ITSELF. Stacked, this would be the ~200ms of dwell still
    // standing when the release landed; skipped, the release IS the return.
    // Generous enough that a loaded machine cannot fail it, tight enough that
    // two thirds of a 300ms dwell cannot hide inside it.
    REQUIRE(afterRelease < std::chrono::milliseconds(150));
}

TEST_CASE("cancelPacing() releases a PAUSED pace(), and still does not throw", "[bridge]")
{
    // THE ONE THAT MATTERS, and the reason the pause was built before any menu
    // stood on it. requestStop() cannot reach a thread inside pace() - the
    // engine reads that flag at boundaries and the thread is not at one - and a
    // paused pace() has no timeout to expire either. So cancelPacing() is the
    // ONLY thing that can end this wait, and GameController::stop() calling it
    // unconditionally is what makes a close land on a paused game rather than
    // hanging in thread.wait() forever.
    //
    // Its non-throwing half is re-asserted here for the same reason the first
    // case in this file asserts it: a pace() that threw would take the clean
    // shutdown path away, and now it would do so on the one path a player
    // reaches by pausing and then closing the window.
    GameEngine engine;
    engine.start(buildSetup());

    GameBridge bridge(std::nullopt, instant());
    bridge.setPaused(true);

    std::atomic<bool> returnedNormally{false};
    std::atomic<bool> threw{false};

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

    // Observed rather than asserted, for the reason the case above gives at
    // length: an assertion here unwinds past an unjoined thread.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const bool heldWhilePaused = !returnedNormally && !threw;

    // NOT setPaused(false). The point is that a close does not have to know the
    // game was paused - stop() calls this and nothing else, and it is enough.
    bridge.cancelPacing();

    // BOUNDED, WITH A RESCUE. The mutation this case exists to catch is the
    // realistic one - a paused wait written `[this]{ return !pacingPaused; }`,
    // forgetting the cancel - and under it the cancel above releases nothing,
    // so join() would hang the whole suite rather than fail this assertion.
    //
    // The rescue has to be setPaused(false) and cannot be another cancel: the
    // cancel is precisely what is broken. Lifting the pause is the one thing
    // that still ends the wait, which makes the failure printable.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!returnedNormally && !threw && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    const bool cancelReleasedIt = returnedNormally || threw;

    if(!cancelReleasedIt)
        bridge.setPaused(false);

    worker.join();

    REQUIRE(heldWhilePaused);
    REQUIRE(cancelReleasedIt);
    REQUIRE(returnedNormally);
    REQUIRE_FALSE(threw);
}

TEST_CASE("A resumed game is not still paused at its next beat", "[bridge]")
{
    // The counterweight, in the shape the file's own "pace() actually waits"
    // case uses: the two tests above would both pass against a setPaused() that
    // latched permanently on its first true and cleared on its first false, and
    // also against one that only ever consulted the flag once.
    //
    // What this pins is that the flag is READ AT EVERY WAIT rather than at the
    // first one - so a game paused and resumed goes on to pace normally, which
    // is the entire behaviour a player sees after pressing Resume.
    GameEngine engine;
    engine.start(buildSetup());

    GameBridge bridge(std::nullopt, instant());

    bridge.setPaused(true);
    bridge.setPaused(false);

    const auto started = std::chrono::steady_clock::now();
    bridge.onRoundScored(engine);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    // On the test's OWN thread, with nothing standing by to release it: a
    // pace() still holding the pause would deadlock here rather than fail, so
    // the timeout is the suite's rather than an assertion's. That is the
    // honest shape - there is no way to observe "it did not block" except by
    // not blocking.
    REQUIRE(elapsed < std::chrono::seconds(5));
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

TEST_CASE("The round result reports the round it was scored in", "[bridge]")
{
    // SECTION 21.3's PAYLOAD, and the assertion is about WHEN it is built rather
    // than what is in it. onRoundScored() is the last callback at which
    // getCurrentRoundIndex() still names the round being reported; by
    // onRoundComplete() the index has advanced and every number would be
    // attributed to the next round.
    //
    // Driven through a real seeded game rather than a hand-built engine, so the
    // numbers are the engine's own.
    GameEngine engine;
    engine.start(fixtures::buildSetup(2, 42u));

    GameBridge bridge(std::nullopt, instant());

    std::vector<RoundResult> results;
    QObject::connect(&bridge, &GameBridge::roundScored,
                     [&results](RoundResult result){ results.push_back(std::move(result)); });

    engine.addObserver(&bridge);
    engine.run();

    REQUIRE_FALSE(results.empty());
    REQUIRE(results.size() == engine.getRoundCount());

    // ONE PER ROUND, IN ORDER, EACH NAMING ITS OWN. A result built one callback
    // later would be off by one everywhere but the last round - where the index
    // does not advance at all, which is A6's finding and the reason an off-by-one
    // here would otherwise hide in the final row.
    for(std::size_t i = 0 ; i < results.size() ; i++)
    {
        INFO("result " << i);
        REQUIRE(results[i].roundNumber == i + 1);
        REQUIRE(results[i].roundCount == engine.getRoundCount());
        REQUIRE(results[i].trickCount == engine.getRound(static_cast<unsigned int>(i)).getTrickCount());
        REQUIRE(results[i].roundType == engine.getRound(static_cast<unsigned int>(i)).getRoundType());
        REQUIRE(results[i].seats.size() == engine.getPlayerCount());
    }
}

TEST_CASE("The round result carries the score and the total it is not yet in", "[bridge]")
{
    // THE TWO HALVES OF "34 -> 41". IGameObserver.h is explicit that inside this
    // callback getRoundScore() is what the round was worth and getTotalScore()
    // is the committed total it has NOT been folded into - so the projection is
    // their sum, and a client that read one for the other would be wrong on
    // every row of every sheet.
    //
    // Asserted as a RUNNING TOTAL across the game, which is what makes it a test
    // of the split rather than of arithmetic: each round's `totalScore` must
    // equal the previous round's projection.
    GameEngine engine;
    engine.start(fixtures::buildSetup(2, 42u));

    GameBridge bridge(std::nullopt, instant());

    std::vector<RoundResult> results;
    QObject::connect(&bridge, &GameBridge::roundScored,
                     [&results](RoundResult result){ results.push_back(std::move(result)); });

    engine.addObserver(&bridge);
    engine.run();

    REQUIRE(results.size() > 1);

    // The first round starts from nothing committed.
    for(const RoundSeatResult& seat : results.front().seats)
        REQUIRE(seat.totalScore == 0);

    for(std::size_t i = 1 ; i < results.size() ; i++)
    {
        for(std::size_t s = 0 ; s < results[i].seats.size() ; s++)
        {
            const RoundSeatResult& now = results[i].seats[s];
            const RoundSeatResult& before = results[i - 1].seats[s];

            INFO("round " << i + 1 << ", seat " << now.name);
            REQUIRE(now.totalScore == before.totalScore + before.roundScore);
        }
    }

    // And the last projection is the score the game actually ends on, which ties
    // the whole chain to something outside it.
    for(const Standing& standing : engine.getStandings())
    {
        const RoundSeatResult& last = results.back().seats.at(standing.seat.index);

        INFO("final standing for " << standing.name);
        REQUIRE(last.totalScore + last.roundScore == standing.score);
    }
}

TEST_CASE("The round result says whether the NEXT round counts for streaks", "[bridge]")
{
    // THE ONE FIELD NO SNAPSHOT COULD CARRY, and the reason RoundResult exists
    // rather than the sheet reading lastSnapshot. One-card rounds count for
    // neither direction of a streak, so section 16.1's "a fifth costs 10" is
    // wrong whenever the next round deals one card - and only the game thread
    // can ask getRound(index + 1).
    GameEngine engine;
    engine.start(fixtures::buildSetup(2, 42u));

    GameBridge bridge(std::nullopt, instant());

    std::vector<RoundResult> results;
    QObject::connect(&bridge, &GameBridge::roundScored,
                     [&results](RoundResult result){ results.push_back(std::move(result)); });

    engine.addObserver(&bridge);
    engine.run();

    REQUIRE(results.size() == engine.getRoundCount());

    for(std::size_t i = 0 ; i < results.size() ; i++)
    {
        const bool isLast = i + 1 == results.size();

        INFO("round " << i + 1 << " of " << results.size());

        if(isLast)
        {
            // NO NEXT ROUND TO COUNT. The default schedule ends with a Forehead
            // and a Hidden round, both one card, so a version that read this off
            // the CURRENT round would also answer false here - which is why the
            // loop below matters more than this line.
            REQUIRE_FALSE(results[i].nextRoundCountsForStreaks);
            continue;
        }

        const unsigned int nextCards =
            engine.getRound(static_cast<unsigned int>(i) + 1).getTrickCount();

        REQUIRE(results[i].nextRoundCountsForStreaks == (nextCards != 1));
    }

    // AND BOTH ANSWERS ACTUALLY OCCUR in this schedule, so the loop above cannot
    // pass by the field being constant. A 1-8-1 game opens with one-card rounds
    // and climbs, so it has both.
    const auto counts = [&results](bool value)
    {
        return std::count_if(results.begin(), results.end(),
                             [value](const RoundResult& r)
                             { return r.nextRoundCountsForStreaks == value; });
    };

    REQUIRE(counts(true) > 0);
    REQUIRE(counts(false) > 0);
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

TEST_CASE("The lead rhombus marks the seat the engine has leading, across a real game", "[bridge]")
{
    // trickitem::leadSeat() reads the snapshot's own fields - the trick's first
    // entry, else the seat on turn, and only once every bid is in - rather than
    // the engine's trick leader, which moves to the winner as a trick is won. What
    // has to hold for that to be right is a fact about the bridge's frames, so it is
    // checked over a whole schedule here, the way the case above checks activeSeat:
    //
    //   - with nothing on the table, a seat it marks is the one the engine will
    //     have lead (snapshot.leadSeat is getTrickLeaderSeat() then) - including
    //     onBettingComplete's frame, where a seat still on turn from the bidding
    //     would be the wrong one;
    //   - once a round is over and its table cleared, it marks nobody, though the
    //     engine's leader still names somebody.
    GameBridge bridge(std::nullopt, instant());

    std::shared_ptr<const GameSnapshot> latest;
    QObject::connect(&bridge, &GameBridge::stateChanged,
                     [&latest](std::shared_ptr<const GameSnapshot> snapshot)
                     { latest = std::move(snapshot); });

    // After the bridge, so the latest snapshot is this callback's own - see above.
    class LeadChecker : public IGameObserver
    {
    public:
        explicit LeadChecker(const std::shared_ptr<const GameSnapshot>& _latest)
            : latest(_latest)
        {}

        void onBettingComplete(const GameEngine&) override { checkEmpty(); }
        void onTrickStarted(const GameEngine&, unsigned int, Seat) override { checkEmpty(); }
        void onCardRequested(const GameEngine&, Seat) override { checkEmpty(); }
        void onRoundComplete(const GameEngine&) override { checkOver(); }
        void onGameOver(const GameEngine&) override { checkOver(); }

        unsigned int marked = 0;
        unsigned int wrong = 0;

    private:
        void checkEmpty()
        {
            if(!latest || !latest->table.empty())
                return;

            const std::optional<unsigned int> lead = trickitem::leadSeat(*latest);

            if(!lead)
                return;

            ++marked;

            if(*lead != latest->leadSeat)
                ++wrong;
        }

        void checkOver()
        {
            if(latest && latest->table.empty() && trickitem::leadSeat(*latest))
                ++wrong;
        }

        const std::shared_ptr<const GameSnapshot>& latest;
    };

    LeadChecker checker(latest);

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.addObserver(&checker);
    engine.start(fixtures::buildSetup(4));
    engine.run();

    // Some frame had to mark a leader before a card was down, or this checked
    // nothing: every trick's lead request is one.
    REQUIRE(checker.marked > 0);
    REQUIRE(checker.wrong == 0);
}
