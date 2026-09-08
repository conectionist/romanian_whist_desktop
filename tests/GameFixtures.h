#ifndef GAME_FIXTURES_H
#define GAME_FIXTURES_H

#include "engine/GameBridge.h"
#include "engine/GameController.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <QObject>
#include <QString>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// What every test that drives a real game needs, in one place.
//
// GameBridgeTests and GameControllerTests each carried an identical private
// copy; A6 wanted a third with a different seat count, which is the point at
// which copying stops being cheaper than sharing. C1's HumanSeatTests wanted
// Outcome on the same grounds, which is why that moved here too.

namespace fixtures
{

// Zero pacing, so a whole 26-round game runs in milliseconds. Every publish is
// followed by one of these waits, and at 250ms a card a full schedule would be
// several minutes per test case.
inline Pacing instant()
{
    return Pacing{ std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0) };
}

// DETERMINISTIC STRATEGIES ONLY, and never RandomCardStrategy even seeded: its
// draw count depends on what every other seat bids, so any behavioural change
// desyncs its stream and turns a located difference into "something changed
// somewhere". The engine's own golden suite excludes it for the same reason.
//
// Two seats by default because that is what the shutdown and bridge suites
// want - the fewest that make a game - and four when a caller needs a table
// with a real trick in it.
//
// `humanSeat` LEAVES THAT SEAT'S PROVIDER NULL, which is exactly what a caller
// of GameController::start() is contracted to do: start() fills that one seat,
// and only that one, with a SeatProvider forwarding to the QtMoveProvider the
// controller owns. Engaging it here without calling prepare() with the same seat
// builds a setup the engine rejects by name, which is a legitimate thing for a
// test to want and the reason this is a plain parameter rather than something
// cleverer.
inline romanian_whist::GameSetup buildSetup(std::size_t seatCount = 2,
                                            std::optional<std::uint32_t> seed = 42u,
                                            std::optional<std::size_t> humanSeat = std::nullopt)
{
    using namespace romanian_whist;

    // Six, which is the most seats the engine accepts. A caller asking for more
    // is a test bug, and the fallback is what lets the ENGINE say so: indexing
    // past the end here would be undefined behaviour reached before
    // GameEngine::start() ever gets to validate the count.
    static const std::array<std::string, 6> names =
        { "Ana", "Bogdan", "Cristina", "Dan", "Elena", "Florin" };

    GameSetup setup;

    for(std::size_t i = 0 ; i < seatCount ; i++)
    {
        const std::string name = i < names.size() ? names[i]
                                                  : "Seat " + std::to_string(i + 1);

        if(humanSeat && *humanSeat == i)
        {
            setup.seats.push_back(SeatSetup{ name, nullptr });
            continue;
        }

        std::unique_ptr<IStrategy> strategy;

        switch(i % 3)
        {
            case 0:  strategy = std::make_unique<LowRiskStrategy>();    break;
            case 1:  strategy = std::make_unique<DuckingStrategy>();    break;
            default: strategy = std::make_unique<FirstCardStrategy>();  break;
        }

        setup.seats.push_back(SeatSetup{ name,
                                         std::make_unique<AiMoveProvider>(std::move(strategy)) });
    }

    setup.structure = GameStructure::S_181;

    // Left at its default, which appends a Forehead round and then a Hidden
    // one. A6 asserts on both, so this is load-bearing rather than incidental.
    setup.endWithForeheadAndHidden = true;

    setup.shuffleSeed = seed;

    return setup;
}

// The lowest bid a prompt permits: 0, or 1 where 0 is the forbidden one.
//
// IN THE TEST TREE SINCE C2, and it used to be promptstandin::chooseBet() beside
// the client's own stand-in. BidStrip took its only production caller, and a
// function in src/ that only tests call is worse than one that lives with them -
// so it moved here, where the seated-game tests still need something to answer a
// bid with. C3 does the same to chooseSlot() and deletes that file.
//
// NEVER prompt.forbiddenBet. QtMoveProvider::makeBet() re-prompts rather than
// handing an illegal bet to the engine, so getting this wrong is a prompt LOOP
// rather than a failure - the game never ends and the assertions after it are
// unreachable. That is the hazard kPromptCeiling in HumanSeatTests exists for.
//
// Walked rather than written as `forbidden == 0 ? 1 : 0`, which is the same
// answer everywhere it is defined and undefined in the one place it is not: a
// 0-card round would have that bid 1 against a hand of none.
inline unsigned int lowestLegalBid(const BetPrompt& prompt)
{
    for(std::size_t bet = 0 ; bet <= prompt.handSize ; bet++)
    {
        const unsigned int candidate = static_cast<unsigned int>(bet);

        if(!prompt.forbiddenBet || candidate != *prompt.forbiddenBet)
            return candidate;
    }

    // Unreachable: forbiddenBet is one value and the range holds at least one
    // more, every round the engine schedules having at least one card.
    return 0;
}

// The first legal slot in a play prompt, and 0 in a blind round where every slot
// is playable and there are no entries to walk.
//
// IN THE TEST TREE SINCE C3, where HandItem took its production caller. It was
// promptstandin::chooseSlot(), the second and last half of the stand-in that
// unparked the engine from C1 until the widgets existed - see
// fixtures::lowestLegalBid() above, which made the same move in C2.
//
// FIRST LEGAL, NOT FIRST. playCard() re-prompts rather than handing the engine an
// illegal card, so answering with slot 0 regardless is a prompt LOOP rather than
// a failure: the game never ends and every assertion after it is unreachable.
// That is the hazard kPromptCeiling in HumanSeatTests exists for.
inline std::size_t firstLegalSlot(const PlayPrompt& prompt)
{
    for(std::size_t slot = 0 ; slot < prompt.entries.size() ; slot++)
    {
        if(prompt.entries[slot].legal)
            return slot;
    }

    // Two ways here, and both mean 0: a blind round, where entries is empty and
    // handIndex still carries the one position; and a hand with entries but
    // nothing legal, which playCard() never raises a prompt for at all.
    return 0;
}

// Records which of the three terminal signals fired, and lets the test thread
// wait for any of them with a deadline.
//
// Connected DIRECT, so the handler runs on the game thread as the signal is
// emitted. The alternative - a queued connection into this thread - would need
// the event loop pumped to see anything, which some tests exercise deliberately
// and the rest have no reason to.
//
// ON THE BRIDGE, all three, which is the seam that reaches the game thread.
// GameController's identically named signals are the GUI's seam: mirrors,
// re-emitted on this thread once the event loop runs. A test that wants to watch
// a game as it happens wants the bridge; a test that wants to see what a window
// would see wants the controller. gameFailed in particular is a MIRROR now, so a
// direct connection to the controller's copy never fires without the event loop
// pumped and await() would simply time out.
//
// Through a context object rather than context-free, which is what disconnects
// these when the Outcome dies. An Outcome is often a scoped local while its
// sender is not - a controller outlives the inner scope of a reuse test - so a
// context-free lambda would leave a dangling `this` connected for game two.
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
                         [this](std::vector<romanian_whist::Standing>,
                                std::vector<romanian_whist::Standing>)
                         { record(overCount); },
                         Qt::DirectConnection);

        QObject::connect(controller.bridge(), &GameBridge::gameStopped, &context,
                         [this]{ record(stoppedCount); },
                         Qt::DirectConnection);

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

} // namespace fixtures

#endif
