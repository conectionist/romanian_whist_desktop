#include "GameController.h"

#include "GameAbandoned.h"

#include <QtGlobal>

#include <exception>
#include <memory>
#include <mutex>
#include <utility>

using romanian_whist::GameEngine;
using romanian_whist::GameSetup;
using romanian_whist::Seat;

GameController::GameController(QObject* parent) : QObject(parent)
{}

GameController::~GameController()
{
    // In the body, so the thread is joined before any member below is
    // destroyed. gameBridge has game-thread affinity by then.
    stop();
}

void GameController::prepare(std::optional<Seat> humanSeat, Pacing pacing)
{
    {
        // Before anything is built. The previous game's stop() set this, and a
        // flag left latched is read back by the next game's run slot, which then
        // stops it before the first card is dealt.
        std::lock_guard lock(engineMutex);
        stopRequested = false;
        engine = nullptr;
    }

    // ON THE GUI THREAD, both of them, and handed back so the caller can connect
    // before the thread exists.
    gameBridge = std::make_unique<GameBridge>(humanSeat, pacing);

    // A spectator game gets no provider at all: there is no human seat for one
    // to serve, and stop() is written knowing this pointer is null for the whole
    // of phase A.
    movePlayer = humanSeat ? std::make_unique<QtMoveProvider>(gameBridge.get())
                           : nullptr;
}

void GameController::start(GameSetup setup)
{
    Q_ASSERT_X(gameBridge != nullptr, "GameController::start", "prepare() must be called first");

    // The human seat's provider is left null by the caller and filled in here -
    // with the non-owning forwarder, not the real provider, so that the engine
    // destroying its GameSetup on the game thread cannot take the object a click
    // handler still points at.
    if(movePlayer)
    {
        for(romanian_whist::SeatSetup& seat : setup.seats)
        {
            if(!seat.moveProvider)
                seat.moveProvider = std::make_unique<SeatProvider>(movePlayer.get());
        }
    }

    // The bridge belongs to the game thread from here on. Its signals are
    // AutoConnection, which now resolves to QueuedConnection for anything
    // connected from the GUI thread - and marshals for us.
    gameBridge->moveToThread(&thread);

    // Replaced, not appended. connect() adds a connection every time it is
    // called, so a controller reused for a second game would end up with two run
    // slots on QThread::started - two engines, one bridge, on one thread.
    QObject::disconnect(runConnection);

    // Worker-object pattern, never a QThread subclass. GameSetup is move-only,
    // so it travels in the lambda's captures; it could not go through a queued
    // signal argument at all.
    //
    // No context object, which makes this a DIRECT connection - so the lambda
    // runs on the new thread, inside QThread::run(), before its event loop is
    // entered. That is what puts the engine's whole life on the game thread.
    runConnection = QObject::connect(&thread, &QThread::started,
                                     [this, setup = std::move(setup)]() mutable
                                     { runGame(std::move(setup)); });

    thread.start();
}

void GameController::runGame(GameSetup setup)
{
    // THE ENGINE IS A LOCAL, born and destroyed on this thread. It is neither
    // copyable nor movable (it holds an atomic stop flag), so there is nothing
    // else it could be.
    GameEngine game;

    try
    {
        // Registered before start(), because start() fires onGameStarted() as
        // its last act and a bridge attached afterwards would miss it.
        game.addObserver(gameBridge.get());

        game.start(std::move(setup));

        {
            // PUBLISH AND READ BACK UNDER ONE LOCK. That is the whole of the fix
            // for a stop that lands in the gap between thread.start() and here:
            // a stop() arriving before this lock is taken sets the flag and is
            // read on the next line, and one arriving after it finds the pointer
            // and calls requestStop() itself. There is no third ordering, which
            // is the only reason this is two lines rather than a handshake.
            std::lock_guard lock(engineMutex);
            engine = &game;

            if(stopRequested)
                game.requestStop();   // honoured at the first boundary run() reaches
        }

        game.run();
    }
    catch(const GameAbandoned&)
    {
        // Expected, and the one that reports nothing: the provider was woken by
        // abandon() because the window is closing. Nobody is left to tell.
    }
    catch(const std::exception& e)
    {
        // A catch-all for the reason the shutdown table has three rows rather
        // than one: the interesting failure is always the one nobody wrote a row
        // for. A bet the UI failed to constrain, a std::logic_error out of the
        // engine's re-entrancy guards, a std::bad_alloc - all of them unwind
        // run() exactly as GameAbandoned does, and any of them escaping this
        // slot is std::terminate rather than a message.
        emit gameFailed(QString::fromUtf8(e.what()));
    }
    catch(...)
    {
        emit gameFailed(QStringLiteral("the game thread failed"));
    }

    // Withdrawn before the local dies, so the GUI thread cannot find a pointer
    // to an engine that is being destroyed. Outside the try: it has to happen on
    // every path out, including the two that threw.
    {
        std::lock_guard lock(engineMutex);
        engine = nullptr;
    }

    // The game is over, so the thread's work is done. QThread::run() enters its
    // event loop the moment this slot returns, and nothing will ever post to it
    // - so without this the thread sits in exec() until stop() happens to call
    // quit(), and "the game ended" and "the thread ended" are different events
    // for no reason.
    //
    // Safe from this thread, and safe before exec() is entered: QThread::exit()
    // latches, and exec() checks that latch on entry and returns at once.
    thread.quit();
}

void GameController::stop()
{
    {
        // Under the lock, and null-checked: the engine is built by the run slot
        // on the other thread, so a window closed in the gap between
        // thread.start() and that construction finds nothing here - and one
        // closed after a game that ended on its own would otherwise dereference
        // a pointer the game thread has already destroyed. requestStop() is the
        // one engine member callable from here at all.
        std::lock_guard lock(engineMutex);

        // SET FIRST, and set unconditionally. The null case is not "nothing to
        // do", it is "nobody to tell yet": there is no engine whose flag could
        // be raised, so the request is recorded here and the run slot honours it
        // under this same lock the moment it publishes the pointer.
        //
        // Dropping it instead is the one path on which a closed window lets the
        // game play itself out to onGameOver().
        stopRequested = true;

        if(engine != nullptr)
            engine->requestStop();   // ends cleanly at the next round, bid or trick boundary
    }

    // NULL-CHECKED, and not defensively: a spectator game has no human seat and
    // so no provider at all, and phase A is a spectator game - so the very first
    // game this client ever plays reaches this line with nothing there.
    if(movePlayer)
        movePlayer->abandon();       // unblocks a provider parked mid-prompt

    // Guarded too, for a narrower reason: prepare() builds it, so this is null
    // only if stop() is reached before prepare() ever ran - a window closed
    // during setup, or a controller that was never used.
    if(gameBridge)
        gameBridge->cancelPacing();  // unblocks an interruptible pace(), which RETURNS

    // quit() does nothing until run() has returned: the slot is blocking that
    // event loop, which is the whole point of the calls above. They are what end
    // the game; this pair only joins the thread afterwards.
    thread.quit();
    thread.wait();                   // join BEFORE anything else is destroyed

    // Everything above is idempotent already - a second requestStop() on a
    // stopped game is a no-op the engine documents, abandon() and cancelPacing()
    // only re-set flags that are already set, and quit()/wait() on a finished
    // thread return at once. So there is deliberately no `stopped` guard here;
    // there would be nothing for it to guard.
}
