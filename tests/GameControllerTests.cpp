#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "engine/GameController.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
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
// Zero pacing, so a whole 26-round game runs in milliseconds. Every publish is
// followed by one of these waits, and at 700ms a card a full schedule would be
// a very long test.
Pacing instant()
{
    return Pacing{ std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0) };
}

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

// Records which of the two terminal signals fired, and lets the test thread
// wait for either with a deadline.
//
// Connected DIRECT, so the handler runs on the game thread as the signal is
// emitted. The alternative - a queued connection into this thread - would need
// the event loop pumped to see anything, which is what the last tests in this
// file exercise deliberately and the rest have no reason to.
//
// ON THE BRIDGE, all three, which is the seam that reaches the game thread.
// GameController's identically named signals are the GUI's seam: mirrors,
// re-emitted on this thread once the event loop runs. A test that wants to
// watch a game as it happens wants the bridge; a test that wants to see what a
// window would see wants the controller.
//
// Through a context object rather than context-free, which is what disconnects
// these when the Outcome dies. An Outcome is a scoped local while its sender is
// not: `controller` outlives the reuse test's inner scope, so a context-free
// lambda leaves a dangling `this` connected for game two.
//
// MUST NOT BE DESTROYED MID-EMIT. Qt's disconnect is thread-safe, but a slot
// already entered runs to completion - so an Outcome goes out of scope only
// after await() has returned and the game has been stopped or has ended.
class Outcome
{
public:
    explicit Outcome(GameController& controller)
    {
        QObject::connect(controller.bridge(), &GameBridge::gameOver, &context,
                         [this](std::vector<Standing>, std::vector<Standing>)
                         { record(overCount); },
                         Qt::DirectConnection);

        QObject::connect(controller.bridge(), &GameBridge::gameStopped, &context,
                         [this]{ record(stoppedCount); },
                         Qt::DirectConnection);

        // ON THE BRIDGE, like the two above, and it has to be: GameController's
        // gameFailed is a MIRROR now, re-emitted from a queued relay on this
        // thread. A direct connection to the mirror never fires without the
        // event loop pumped, and await() below would simply time out.
        QObject::connect(controller.bridge(), &GameBridge::gameFailed, &context,
                         [this](QString what)
                         {
                             {
                                 std::lock_guard lock(mutex);
                                 failure = what.toStdString();
                             }
                             record(failedCount);
                         },
                         Qt::DirectConnection);
    }

    // Returns false on timeout rather than hanging.
    bool await(std::chrono::milliseconds within = std::chrono::seconds(10))
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, within, [this]{ return finished; });
    }

    std::atomic<int> overCount{0};
    std::atomic<int> stoppedCount{0};
    std::atomic<int> failedCount{0};

    std::string failureMessage()
    {
        std::lock_guard lock(mutex);
        return failure;
    }

private:
    void record(std::atomic<int>& counter)
    {
        counter++;

        {
            std::lock_guard lock(mutex);
            finished = true;

            // NOTIFIED UNDER THE LOCK, which is not the usual advice and is
            // required here. await() returning is what lets the test destroy
            // this Outcome, and several tests scope one tightly so it dies
            // before the next prepare(). Notify after releasing and the waiter
            // can return, run its assertions, leave the scope and reach
            // ~condition_variable while this thread is still inside
            // notify_all() - destroying a condition variable out from under a
            // notifier is undefined behaviour, and ThreadSanitizer reports it
            // as pthread_cond_destroy racing pthread_cond_broadcast.
            //
            // Holding the mutex across the notify closes it: the waiter cannot
            // return from wait_for() until it reacquires the mutex, and it
            // cannot do that until this scope ends.
            cv.notify_all();
        }
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    std::string failure;

    // LAST, so it is destroyed FIRST: Qt drops every connection made with it as
    // context before the mutex and the condition variable those handlers touch
    // are gone.
    QObject context;
};
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
