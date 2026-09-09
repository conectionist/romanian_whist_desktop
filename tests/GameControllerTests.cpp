#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameFixtures.h"

#include "engine/GameController.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <QCoreApplication>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace romanian_whist;

// The shutdown story, which is the part of this client that bites.
//
// Every test here drives a REAL engine on a REAL worker thread. Nothing waits
// without a deadline: a threading test that hangs prints nothing at all, which
// is a far worse way to learn an assertion broke than being told.

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

// A provider that throws something the client never wraps, to reach the run
// slot's catch-all rather than its GameAbandoned arm.
class ThrowingProvider : public IMoveProvider
{
public:
    unsigned int makeBet(const BetContext&) override
    {
        throw std::runtime_error("bot exploded");
    }

    std::optional<std::size_t> playCard(const PlayContext&) override
    {
        throw std::runtime_error("bot exploded");
    }
};

// IN tests/GameFixtures.h SINCE C1, where HumanSeatTests wanted a second copy
// of it. The same rule that put instant() and buildSetup() there and that the
// fixture header states outright: a third copy is the point at which copying
// stops being cheaper than sharing.
//
// Named here rather than qualified at each use so every call site below reads
// exactly as it did.
using fixtures::Outcome;
}

TEST_CASE("stop() before prepare() does nothing and does not crash", "[controller]")
{
    // A window closed during setup, or a controller that was never used. Two of
    // the three pointers stop() touches are null here.
    GameController controller;

    controller.stop();
    controller.stop();

    REQUIRE(controller.bridge() == nullptr);
    REQUIRE(controller.provider() == nullptr);
}

TEST_CASE("A spectator game has no provider at all", "[controller]")
{
    // Not an edge case to bolt on later: this is the state phase A is in for its
    // entire life, and an unguarded movePlayer->abandon() in stop() crashes on
    // the very close phase A exists to test.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    REQUIRE(controller.bridge() != nullptr);
    REQUIRE(controller.provider() == nullptr);

    controller.stop();
}

TEST_CASE("A game left to itself reaches onGameOver", "[controller]")
{
    GameController controller;
    controller.prepare(std::nullopt, instant());

    Outcome outcome(controller);

    controller.start(buildSetup());

    REQUIRE(outcome.await());
    REQUIRE(outcome.overCount == 1);
    REQUIRE(outcome.stoppedCount == 0);
    REQUIRE(outcome.failedCount == 0);

    controller.stop();
}

TEST_CASE("stop() after the game finished on its own is safe", "[controller]")
{
    // The ORDINARY case, not an edge one: the player reads the standings and
    // then closes the window. By then the run slot has returned and the engine
    // is destroyed, so every line of stop() is either guarded or operating on an
    // object the controller still owns.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    Outcome outcome(controller);
    controller.start(buildSetup());

    REQUIRE(outcome.await());
    REQUIRE(outcome.overCount == 1);

    controller.stop();
    controller.stop();      // and again, because a window close is followed by ~GameController

    REQUIRE(outcome.overCount == 1);
    REQUIRE(outcome.stoppedCount == 0);
}

TEST_CASE("A stop while the game is parked on a prompt ends it", "[controller]")
{
    // C5'S PATH, AND THE ONLY ONE THAT SHOULD PRODUCE GameAbandoned. C2's bid
    // strip is what makes it reachable in the running client: the engine sits in
    // makeBet() for as long as the player takes to decide, which is unbounded,
    // and the window has to stay closable throughout. That is the whole reason
    // section 4.4 insists the strip is a panel rather than a QDialog - exec()
    // would take the close button with it and this could not be performed at
    // all.
    //
    // NOTHING COVERED IT UNTIL NOW. Every other shutdown case here is a
    // spectator or an all-bot game, and neither ever parks: they are asleep in
    // pace(), which cancelPacing() returns from. A parked PROVIDER is the other
    // half of section 6 and is woken by abandon(), which THROWS.
    GameController controller;
    controller.prepare(Seat{ 1u }, instant());

    REQUIRE(controller.provider() != nullptr);

    // Answers nothing, which is the point - it only reports that the engine has
    // actually reached the prompt. Stopping before it gets there would exercise
    // the latch instead, which the case below already covers.
    std::atomic<bool> parked{false};

    QObject prompts;

    QObject::connect(controller.bridge(), &GameBridge::betRequested, &prompts,
                     [&parked](BetPrompt){ parked = true; },
                     Qt::DirectConnection);

    Outcome outcome(controller);

    controller.start(fixtures::buildSetup(2, 42u, 1));

    // Spun on rather than slept past: the deal has to happen first, and how long
    // that takes is not something to guess at. Deadlined, because a test that
    // hangs prints nothing.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while(!parked)
    {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // THE JOIN IS THE ASSERTION, and it is made on another thread because
    // stop() is the thing that can fail here and it fails by NEVER RETURNING.
    // If abandon() does not reach the parked provider, stop() sits in
    // thread.wait() for good - requestStop() cannot help, because the game
    // thread is not at a boundary, it is inside a condition variable. Deleting
    // that one line from stop() was measured: this case does not fail, it hangs,
    // and the run has to be killed with no output at all.
    //
    // So the wait gets a deadline like every other in this file, and the rescue
    // below turns the hang into a message. Same shape as kPromptCeiling in
    // HumanSeatTests, from the other side of the same seam.
    std::atomic<bool> returned{false};

    std::thread stopper([&controller, &returned]
                        {
                            controller.stop();
                            returned = true;
                        });

    const auto stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while(!returned && std::chrono::steady_clock::now() < stopDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const bool joined = returned.load();

    // THE RESCUE, and it is what stop() was supposed to do. Only a latch and a
    // notify, so it is safe to call while stop() is blocked in the join - and
    // without it the stopper thread never finishes, ~thread calls std::terminate
    // and the failure below is never printed.
    if(!joined)
        controller.provider()->abandon();

    stopper.join();

    REQUIRE(joined);

    // AND WHICH ENDING, which is the half that says the throw was caught rather
    // than the game merely finishing. GameAbandoned unwinds run() before any
    // observer callback, so NONE of the three fires: not onGameOver, not
    // onGameStopped, and not gameFailed - the run slot's GameAbandoned arm is
    // the one that reports nothing, because the window is closing and there is
    // nobody left to tell.
    REQUIRE(outcome.overCount == 0);
    REQUIRE(outcome.failedCount == 0);
    REQUIRE(outcome.stoppedCount == 0);
}

TEST_CASE("A stop with no engine to tell is latched and honoured", "[controller]")
{
    // The latch, at its simplest and with no threading in the test at all:
    // stop() before start() finds engine == nullptr, so there is nothing whose
    // flag it could raise. Guarding the pointer stops the crash and LOSES THE
    // REQUEST - which is the bug. The request is recorded on the controller
    // instead, and the run slot reads it back the moment it publishes.
    //
    // thread.wait() here is on a thread that was never started, so it returns at
    // once and nothing blocks.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    Outcome outcome(controller);

    controller.stop();
    controller.start(buildSetup());

    REQUIRE(outcome.await());

    // ASSERTING WHICH ONE FIRED IS THE TEST. A spectator game with pacing
    // cancelled finishes either way and quickly, so "it terminated" says nothing
    // at all. Without the read-back this is a full 26-round game ending in
    // onGameOver.
    REQUIRE(outcome.stoppedCount == 1);
    REQUIRE(outcome.overCount == 0);

    controller.stop();
}

TEST_CASE("A stop landing before the engine is published still ends the game", "[controller]")
{
    // The same latch, but racing a RUNNING game thread - which is the window a
    // window-close actually lands in, and the only shutdown case that is not a
    // crash, so the only one a running window would never report.
    //
    // The window is real and reachable with no test hook in the production code:
    // onGameStarted() fires from inside GameEngine::start(), and publish() emits
    // stateChanged from it - so a DIRECT-connected handler on that signal runs
    // on the game thread, inside the gap between start() and the run slot
    // publishing the engine pointer.
    //
    // stop() MUST BE CALLED FROM ANOTHER THREAD. It joins the game thread, and
    // the game thread is the one being held in the gap: calling it from here
    // deadlocks until the handler's own wait times out, which reads as a pass
    // and takes ten seconds to do it.
    //
    // AND THE RELEASE MUST WAIT FOR THE LATCH ITSELF, which is what
    // isStopRequested() is for. Spinning on a flag the stopper thread sets
    // before it enters stop() proves nothing: the store happens before the latch
    // is raised, so this thread can release the game thread first, and with
    // instant() pacing the game then runs to onGameOver() and fails the
    // assertions at the bottom. That is a flake in the safe direction, but a
    // test that passes by exercising the ordinary post-publish path is not
    // testing the gap it was written for.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    std::mutex mutex;
    std::condition_variable cv;
    bool inTheGap = false;
    bool released = false;

    // Counted at the TOP, before the released check, so it counts every
    // published snapshot rather than only the parked one.
    std::atomic<int> snapshots{0};

    QObject::connect(controller.bridge(), &GameBridge::stateChanged,
                     [&](std::shared_ptr<const GameSnapshot>)
                     {
                         snapshots++;

                         std::unique_lock lock(mutex);

                         if(released)
                             return;      // only the first snapshot, onGameStarted's

                         inTheGap = true;
                         cv.notify_all();

                         // Bounded, so a lost handshake fails the assertions
                         // below rather than hanging the suite.
                         cv.wait_for(lock, std::chrono::seconds(10), [&]{ return released; });
                     });

    Outcome outcome(controller);
    controller.start(buildSetup());

    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(10), [&]{ return inTheGap; }));
    }

    // The engine exists but its pointer is not published, so this can only land
    // in the latch. It blocks in thread.wait() until the handler lets go.
    std::thread stopper([&]{ controller.stop(); });

    // SPUN ON THE LATCH, not on a flag beside it. Deadlined like every other
    // wait in this file: a broken handshake fails the REQUIRE below rather than
    // spinning the suite forever.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while(!controller.isStopRequested() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // THE HANDSHAKE, ASSERTED rather than assumed. Past this line the game
    // thread is still parked inside onGameStarted() and the latch is set, so the
    // run slot's read-back under engineMutex is the only ordering left to test.
    REQUIRE(controller.isStopRequested());

    {
        std::lock_guard lock(mutex);
        released = true;
    }
    cv.notify_all();

    stopper.join();

    REQUIRE(outcome.await());
    REQUIRE(outcome.stoppedCount == 1);
    REQUIRE(outcome.overCount == 0);

    // ONE snapshot, onGameStarted's, and this is the sharper half of the test:
    // playRound() calls honourStopIfRequested() BEFORE dealRound(), and
    // onGameStopped() deliberately publishes nothing - so a game that honoured
    // the latch stopped before the deal rather than merely stopping. Read after
    // the join, so the game thread is done writing it.
    REQUIRE(snapshots == 1);
}

TEST_CASE("setPaused() on a controller with no game is a no-op", "[controller]")
{
    // The bridge is built by prepare(), and the pause is reached from a menu -
    // which is on screen precisely when no game is running. So this is the
    // ordinary state for the caller rather than a defensive case, and the same
    // reasoning stop()'s two null guards are written from.
    GameController controller;

    controller.setPaused(true);
    controller.setPaused(false);

    SUCCEED("setPaused() before prepare() did not dereference a null bridge");
}

TEST_CASE("A stop while the game is paused still ends it", "[controller]")
{
    // SECTION 15.3's HAZARD, at the level the window actually calls it. The
    // bridge test proves cancelPacing() releases a paused pace(); this proves
    // stop() gets there - that the pause does not sit between a close and the
    // join, which is a hang on thread.wait() rather than a failure.
    //
    // A REAL PACING, not instant(): a paused game must be held somewhere, and
    // with a zero dwell the engine would race to the end before the pause was
    // ever asked for. At 30 seconds a game that ignored the stop would time out
    // in await() rather than passing quickly for the wrong reason.
    GameController controller;

    controller.prepare(std::nullopt, Pacing{ std::chrono::milliseconds(30000),
                                             std::chrono::milliseconds(30000),
                                             std::chrono::milliseconds(30000) });

    Outcome outcome(controller);
    controller.start(buildSetup());

    // Long enough for the run slot to publish the engine and reach its first
    // paced callback, so the pause has something to hold.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    controller.setPaused(true);

    // ON ITS OWN THREAD, WITH A DEADLINE AND A RESCUE, and not called inline -
    // which is the whole difficulty of asserting this at all.
    //
    // stop() JOINS. So the failure this case exists to catch - a pace() whose
    // paused wait forgot pacingCancelled, which is the realistic way to write
    // the bug section 15.3 describes - does not make stop() return the wrong
    // answer, it makes stop() never return. Called inline that is a hung suite
    // printing nothing, which is C1's finding about a stand-in that answers
    // wrongly, arriving here from the shutdown side instead.
    //
    // So: run it off-thread, wait with a bound, and if the bound expires reach
    // past the controller to the bridge and cancel the pacing by hand. That
    // releases the game thread, lets stop() finish and the worker join, and
    // turns the hang into an ordinary failed assertion.
    std::atomic<bool> stopped{false};
    std::thread stopper([&]
    {
        controller.stop();
        stopped = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!stopped && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    const bool stopReturnedOnItsOwn = stopped;

    // The rescue, and it must be setPaused(false) rather than another
    // cancelPacing(): the failure being caught here IS a cancel that no longer
    // reaches the paused wait, so cancelling again rescues nothing. Lifting the
    // pause is the one release left. A no-op on the passing path, where stop()
    // has already returned.
    if(!stopReturnedOnItsOwn)
        controller.setPaused(false);

    stopper.join();

    // NOT resumed first, anywhere above. The window's close path does not know
    // or care whether the game was paused - it calls stop(), and stop() has to
    // be enough.
    REQUIRE(stopReturnedOnItsOwn);
    REQUIRE(outcome.await());
    REQUIRE(outcome.stoppedCount == 1);
    REQUIRE(outcome.overCount == 0);
}

TEST_CASE("prepare() clears the latch, so a second game is not stopped by the first", "[controller]")
{
    // The same flag from the other side. Between this and the test above, the
    // pair pin a latch that is wrong when it is missing and wrong when it is
    // left set - the second failing as a game that deals one round and stops.
    GameController controller;

    controller.prepare(std::nullopt, instant());
    {
        Outcome first(controller);
        controller.start(buildSetup());
        controller.stop();                  // sets stopRequested
        REQUIRE(first.await());
    }

    // Nothing is reset in place: a fresh bridge and provider from prepare(),
    // then reconnect, then start.
    controller.prepare(std::nullopt, instant());

    Outcome second(controller);
    controller.start(buildSetup(7u));

    REQUIRE(second.await());

    // Game two must finish on its own rather than stopping in its first round.
    REQUIRE(second.overCount == 1);
    REQUIRE(second.stoppedCount == 0);

    controller.stop();
}

TEST_CASE("A controller is reused after a game that ended on its own", "[controller]")
{
    // THE ORDINARY "PLAY AGAIN" PATH, and the one with no stop() in it: the
    // player reads the standings and presses the button. prepare() is the only
    // thing between that and two separate failures - it frees the bridge and the
    // provider the game thread may still be using, and then start()s a QThread
    // whose `running` flag may still be set, which warns and RETURNS. The second
    // is the quieter one by far: game two simply never runs.
    //
    // THE WINDOW IS HELD OPEN DELIBERATELY. await() returns from inside the
    // gameOver emission, so the run slot is genuinely still executing - but only
    // for microseconds, and a threading test that has to win a race to fail is a
    // test that passes over the bug. The handler below parks the game thread
    // mid-emission instead, which is the same state, held.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    std::mutex mutex;
    std::condition_variable cv;
    bool released = false;

    QObject holder;

    {
        // CONSTRUCTED FIRST, so its handler runs first: Qt invokes slots in
        // connection order, and the holder below never returns until this thread
        // has been woken by the one Outcome notifies.
        Outcome first(controller);

        QObject::connect(controller.bridge(), &GameBridge::gameOver, &holder,
                         [&](std::vector<Standing>, std::vector<Standing>)
                         {
                             // Bounded, so a lost handshake fails the assertions
                             // below rather than hanging the suite.
                             std::unique_lock lock(mutex);
                             cv.wait_for(lock, std::chrono::seconds(10), [&]{ return released; });
                         },
                         Qt::DirectConnection);

        controller.start(buildSetup());

        REQUIRE(first.await());
        REQUIRE(first.overCount == 1);
    }

    // FROM ANOTHER THREAD, and on a timer, because the main thread is about to
    // block in the very thread being held: prepare() joins, and a join nothing
    // releases would sit until the holder's own bound expires - a pass that
    // takes ten seconds to arrive.
    std::thread releaser([&]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        {
            std::lock_guard lock(mutex);
            released = true;
        }

        cv.notify_all();
    });

    controller.prepare(std::nullopt, instant());

    Outcome second(controller);
    controller.start(buildSetup(7u));

    // AFTER start(), not before. Joining it earlier would let game one's thread
    // finish on its own while the main thread waited - which is the state this
    // test exists to avoid being in.
    releaser.join();

    REQUIRE(second.await());
    REQUIRE(second.overCount == 1);
    REQUIRE(second.stoppedCount == 0);
    REQUIRE(second.failedCount == 0);

    controller.stop();
}

TEST_CASE("The provider is attached to the human's seat and no other", "[controller]")
{
    // Which seat the player drives has to be the seat the snapshot marks as
    // theirs - GameBridge was handed the same optional - and "fill every seat
    // whose provider is null" only agrees with that while exactly one is.
    //
    // Here two are: seat 1 is the human's, and seat 2 has been left empty by
    // mistake. Filling both hands the player two seats and the game plays on
    // silently; filling only seat 1 leaves seat 2 for GameEngine::start() to
    // reject, which is a failure with a name on it.
    GameController controller;
    controller.prepare(Seat{ 1u }, instant());

    REQUIRE(controller.provider() != nullptr);

    Outcome outcome(controller);

    GameSetup setup;
    setup.seats.push_back(SeatSetup{ "Ana",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Bogdan", nullptr });   // the human
    setup.seats.push_back(SeatSetup{ "Cristina", nullptr }); // the caller's mistake
    setup.structure = GameStructure::S_181;
    setup.shuffleSeed = 42u;

    controller.start(std::move(setup));

    REQUIRE(outcome.await());
    REQUIRE(outcome.failedCount == 1);
    REQUIRE(outcome.overCount == 0);

    // NAMING THE SEAT IS THE TEST. "It failed" would also be true of a game that
    // rejected the human's own seat; this pins that seat 1 was filled and seat 2
    // was left exactly as the caller left it.
    REQUIRE(outcome.failureMessage() ==
            "GameEngine::start: seat 'Cristina' has no move provider");

    controller.stop();
}

TEST_CASE("An exception out of a provider reaches gameFailed", "[controller]")
{
    // Neither onGameStopped() nor onGameOver() fires on this path, so without
    // gameFailed the GUI never learns the game ended - and the exception
    // escaping a slot invoked from QThread's event loop is std::terminate with
    // no message.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    Outcome outcome(controller);

    GameSetup setup;
    setup.seats.push_back(SeatSetup{ "Ana", std::make_unique<ThrowingProvider>() });
    setup.seats.push_back(SeatSetup{ "Bogdan", std::make_unique<ThrowingProvider>() });
    setup.structure = GameStructure::S_181;
    setup.shuffleSeed = 42u;

    controller.start(std::move(setup));

    REQUIRE(outcome.await());
    REQUIRE(outcome.failedCount == 1);
    REQUIRE(outcome.failureMessage() == "bot exploded");
    REQUIRE(outcome.overCount == 0);
    REQUIRE(outcome.stoppedCount == 0);

    controller.stop();
}

TEST_CASE("Snapshots cross the thread boundary as queued events", "[controller]")
{
    // What the whole seam is for, and the one test here that actually needs the
    // QCoreApplication: a QUEUED connection into this thread, delivered by this
    // thread's event loop rather than run on the game thread.
    //
    // It is also the only thing that would notice a missing metatype - Qt drops
    // an unqueueable argument with a line on stderr and no crash.
    GameController controller;
    controller.prepare(std::nullopt, instant());

    // A context object living on THIS thread, which is what makes
    // AutoConnection resolve to QueuedConnection.
    QObject receiver;

    std::vector<std::shared_ptr<const GameSnapshot>> seen;
    QObject::connect(controller.bridge(), &GameBridge::stateChanged, &receiver,
                     [&seen](std::shared_ptr<const GameSnapshot> snapshot)
                     { seen.push_back(std::move(snapshot)); });

    Outcome outcome(controller);
    controller.start(buildSetup());

    REQUIRE(outcome.await());
    controller.stop();

    // Nothing has been delivered yet: the events are queued on this thread and
    // this thread has not run its event loop. That is the assertion - if these
    // had arrived already, the connection was direct and the snapshots were
    // being built and read on two threads at once.
    REQUIRE(seen.empty());

    QCoreApplication::processEvents();

    REQUIRE_FALSE(seen.empty());

    for(const auto& snapshot : seen)
    {
        REQUIRE(snapshot != nullptr);
        REQUIRE(snapshot->seats.size() == 2);
    }

    // The first is onGameStarted's, which fires before the deal.
    REQUIRE(seen.front()->roundNumber == 1);
    REQUIRE_FALSE(seen.front()->hasTrump);
}

TEST_CASE("A second game is not handed the first game's undelivered events", "[controller]")
{
    // THE REUSE LEAK, from the GUI's side of the seam.
    //
    // prepare() destroys game one's bridge, but Qt purges a posted
    // QMetaCallEvent only when its RECEIVER dies - never its sender. So without
    // the purge in prepare(), everything game one queued is still addressed to a
    // very much alive receiver, and it lands after game two has started: game
    // one's final table painted over game two's deal, and its standings popped a
    // second time.
    //
    // The backlog is guaranteed rather than lucky. This thread never pumps its
    // event loop while game one runs - which is exactly the state a "play again"
    // button is pressed in, since the click is handled and prepare() called
    // without returning to the loop in between.
    GameController controller;

    // CONNECTED ONCE, BEFORE THE FIRST prepare(), to the controller's mirrors -
    // which is the whole point of them. A receiver living on this thread, so
    // AutoConnection resolves to a queued connection.
    QObject receiver;

    std::vector<std::shared_ptr<const GameSnapshot>> delivered;
    int overCount = 0;

    QObject::connect(&controller, &GameController::stateChanged, &receiver,
                     [&delivered](std::shared_ptr<const GameSnapshot> snapshot)
                     { delivered.push_back(std::move(snapshot)); });

    QObject::connect(&controller, &GameController::gameOver, &receiver,
                     [&overCount](std::vector<Standing>, std::vector<Standing>)
                     { overCount++; });

    controller.prepare(std::nullopt, instant());

    // Game one's snapshots, collected DIRECT on the game thread and KEPT ALIVE -
    // held so the identity check at the bottom cannot be fooled by a fresh
    // snapshot landing on a recycled allocation.
    //
    // Written on the game thread, read on this one only after the second
    // prepare() below has joined that thread.
    std::vector<std::shared_ptr<const GameSnapshot>> gameOne;
    QObject::connect(controller.bridge(), &GameBridge::stateChanged,
                     [&gameOne](std::shared_ptr<const GameSnapshot> snapshot)
                     { gameOne.push_back(std::move(snapshot)); });

    {
        // Direct, so this knows the game ended without the loop being pumped -
        // which is the state the assertions below need.
        Outcome first(controller);
        controller.start(buildSetup());
        REQUIRE(first.await());
        REQUIRE(first.overCount == 1);
    }

    // A whole game is sitting in this thread's queue, undelivered.
    REQUIRE(delivered.empty());
    REQUIRE(gameOne.size() > 1);

    controller.prepare(std::nullopt, instant());   // joins, then purges

    // THE ASSERTION THE PURGE EXISTS FOR. Without it the entirety of game one
    // arrives here, before game two has emitted a single frame.
    QCoreApplication::processEvents();
    REQUIRE(delivered.empty());
    REQUIRE(overCount == 0);

    Outcome second(controller);
    controller.start(buildSetup(7u));
    REQUIRE(second.await());
    controller.stop();

    QCoreApplication::processEvents();

    REQUIRE_FALSE(delivered.empty());

    // TWO would re-pop the standings over the new game.
    REQUIRE(overCount == 1);

    // And not one of the delivered snapshots is game one's - by identity, since
    // two spectator games of the same structure look alike frame for frame.
    for(const auto& snapshot : delivered)
        REQUIRE(std::find(gameOne.begin(), gameOne.end(), snapshot) == gameOne.end());
}

TEST_CASE("A second game is not handed the first game's failure", "[controller]")
{
    // The mirror image of the test above, and the reason gameFailed is raised on
    // the bridge rather than emitted by the controller. Emitted there, it would
    // post straight to this receiver instead of into the controller's own queue,
    // and the purge in prepare() could not reach it - so game one's "bot
    // exploded" would be reported over game two.
    GameController controller;

    QObject receiver;
    int failedCount = 0;

    QObject::connect(&controller, &GameController::gameFailed, &receiver,
                     [&failedCount](QString){ failedCount++; });

    controller.prepare(std::nullopt, instant());

    {
        Outcome first(controller);

        GameSetup setup;
        setup.seats.push_back(SeatSetup{ "Ana", std::make_unique<ThrowingProvider>() });
        setup.seats.push_back(SeatSetup{ "Bogdan", std::make_unique<ThrowingProvider>() });
        setup.structure = GameStructure::S_181;
        setup.shuffleSeed = 42u;

        controller.start(std::move(setup));

        REQUIRE(first.await());
        REQUIRE(first.failedCount == 1);
    }

    // Queued at the controller and never pumped, exactly as a "play again"
    // pressed on the failure dialog would leave it.
    REQUIRE(failedCount == 0);

    controller.prepare(std::nullopt, instant());

    QCoreApplication::processEvents();
    REQUIRE(failedCount == 0);
}
