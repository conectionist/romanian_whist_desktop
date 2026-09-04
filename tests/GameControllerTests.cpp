#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "engine/GameController.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <QCoreApplication>

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
// Connected with a CONTEXT-FREE lambda on purpose, so the handler runs on the
// game thread as the signal is emitted. The alternative - a queued connection
// into this thread - would need the event loop pumped to see anything, which is
// what the last test in this file exercises deliberately and the rest have no
// reason to.
class Outcome
{
public:
    explicit Outcome(GameController& controller)
    {
        QObject::connect(controller.bridge(), &GameBridge::gameOver,
                         [this](std::vector<Standing>, std::vector<Standing>)
                         { record(overCount); });

        QObject::connect(controller.bridge(), &GameBridge::gameStopped,
                         [this]{ record(stoppedCount); });

        QObject::connect(&controller, &GameController::gameFailed,
                         [this](QString what)
                         {
                             {
                                 std::lock_guard lock(mutex);
                                 failure = what.toStdString();
                             }
                             record(failedCount);
                         });
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
        }

        cv.notify_all();
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    std::string failure;
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
    GameController controller;
    controller.prepare(std::nullopt, instant());

    std::mutex mutex;
    std::condition_variable cv;
    bool inTheGap = false;
    bool released = false;
    std::atomic<bool> stopping{false};

    QObject::connect(controller.bridge(), &GameBridge::stateChanged,
                     [&](std::shared_ptr<const GameSnapshot>)
                     {
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
    std::thread stopper([&]
    {
        stopping = true;
        controller.stop();
    });

    while(!stopping)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    {
        std::lock_guard lock(mutex);
        released = true;
    }
    cv.notify_all();

    stopper.join();

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
