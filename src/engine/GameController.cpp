#include "GameController.h"

#include "GameAbandoned.h"

#include <QCoreApplication>
#include <QEvent>
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
    // FIRST, before a single member below is replaced, and this is what makes
    // reuse safe rather than merely intended. Two separate failures live in
    // skipping it, and neither is loud:
    //
    //   * assigning over gameBridge and movePlayer destroys the observer the
    //     game thread is publishing through and the provider a parked seat is
    //     blocked in - a use-after-free, not a race with a warning; and
    //   * QThread::start() on a thread that is still running warns and RETURNS,
    //     so game two never runs at all.
    //
    // The window is the ordinary one, not an exotic one: onGameOver() fires from
    // inside the run slot, so a "play again" pressed while the standings are on
    // screen lands squarely in it.
    //
    // stop() is exactly the right call here - it ends the game AND joins, is
    // idempotent, and is a no-op on a controller that has never been used.
    stop();

    {
        // AFTER stop(), which sets this. The previous game's stop() - or the one
        // above - set the latch, and a flag left set is read back by the next
        // game's run slot, which then stops it before the first card is dealt.
        std::lock_guard lock(engineMutex);
        stopRequested = false;
        engine = nullptr;
    }

    // THE PREVIOUS GAME'S UNDELIVERED SIGNALS, dropped on the floor - and this
    // one call is the whole reason the signals above are mirrored here. It is
    // its own function since D1's review, because prepare() stopped being the
    // only moment an old game's queue is worthless: abandoning one to the main
    // menu is another, and that path was leaving the backlog to repaint the
    // status bar over the menu page.
    //
    // AFTER stop(), which prepare() calls first, so that holds by construction.
    discardQueuedSignals();

    // KEPT, because start() needs to know which seat the player drives - see
    // there. Set with the provider below and engaged on exactly the same
    // condition, so `movePlayer != nullptr` and `humanPlayerSeat.has_value()`
    // are two spellings of one fact.
    humanPlayerSeat = humanSeat;

    // ON THE GUI THREAD, both of them. The bridge is relayed below and moved to
    // the game thread by start().
    gameBridge = std::make_unique<GameBridge>(humanSeat, pacing);

    // THE RELAY. Every signal the game thread raises crosses into this object,
    // exactly once, and the GUI hears it from here - see the signals block for
    // why the GUI must not connect to the bridge itself.
    //
    // NEVER DISCONNECTED, and deliberately so: the bridge is the SENDER of all
    // twelve, so every one of them dies with the object the next prepare()
    // destroys. That is the exact opposite of runConnection, whose sender is the
    // long-lived `thread` and which therefore has to be replaced by hand - and
    // getting the two confused in either direction is a bug: connections leaked
    // onto a second game here, or a bridge relayed twice.
    //
    // MADE WHILE THE BRIDGE IS STILL GUI-AFFINE, which costs nothing.
    // AutoConnection resolves per emission rather than at connect time, so these
    // become queued the moment start() moves the bridge to the game thread.
    connect(gameBridge.get(), &GameBridge::stateChanged,  this, &GameController::stateChanged);
    connect(gameBridge.get(), &GameBridge::turnChanged,   this, &GameController::turnChanged);
    connect(gameBridge.get(), &GameBridge::betPlaced,     this, &GameController::betPlaced);
    connect(gameBridge.get(), &GameBridge::trickStarted,  this, &GameController::trickStarted);
    connect(gameBridge.get(), &GameBridge::cardPlayed,    this, &GameController::cardPlayed);
    connect(gameBridge.get(), &GameBridge::trickWon,      this, &GameController::trickWon);
    connect(gameBridge.get(), &GameBridge::roundScored,   this, &GameController::roundScored);
    connect(gameBridge.get(), &GameBridge::gameOver,      this, &GameController::gameOver);
    connect(gameBridge.get(), &GameBridge::gameStopped,   this, &GameController::gameStopped);
    connect(gameBridge.get(), &GameBridge::betRequested,  this, &GameController::betRequested);
    connect(gameBridge.get(), &GameBridge::cardRequested, this, &GameController::cardRequested);
    connect(gameBridge.get(), &GameBridge::gameFailed,    this, &GameController::gameFailed);

    // A spectator game gets no provider at all: there is no human seat for one
    // to serve, and stop() is written knowing this pointer is null for the whole
    // of phase A.
    movePlayer = humanSeat ? std::make_unique<QtMoveProvider>(gameBridge.get())
                           : nullptr;
}

void GameController::start(GameSetup setup)
{
    Q_ASSERT_X(gameBridge != nullptr, "GameController::start", "prepare() must be called first");

    // The other half of the contract, and worth asserting rather than working
    // around: prepare() is the join, so a thread still running here means
    // start() was called twice without one. Ending the running game from inside
    // start() would be the wrong repair - that is stop()'s decision to make, not
    // a side effect of starting the next game.
    Q_ASSERT_X(!thread.isRunning(), "GameController::start",
               "prepare() joins the previous game; a second start() without it is a silent no-op");

    // The human seat's provider is left null by the caller and filled in here -
    // with the non-owning forwarder, not the real provider, so that the engine
    // destroying its GameSetup on the game thread cannot take the object a click
    // handler still points at.
    if(movePlayer)
    {
        // BY SEAT, not by "whichever slot happens to be empty". The seat the
        // player drives has to be the seat the snapshot marks as theirs -
        // GameBridge was handed the same optional in prepare() - and filling
        // every empty slot only agrees with that while there is exactly one.
        // Two, and the player silently drives a seat that is drawn as a bot's.
        //
        // .at(), not [], so a seat past the end of the table is an exception out
        // of start() on the GUI thread rather than undefined behaviour. start()
        // already documents that it can throw and that a retry needs a freshly
        // built GameSetup.
        romanian_whist::SeatSetup& seat = setup.seats.at(humanPlayerSeat->index);

        Q_ASSERT_X(seat.moveProvider == nullptr, "GameController::start",
                   "the human seat's provider is left null by the caller");

        seat.moveProvider = std::make_unique<SeatProvider>(movePlayer.get());
    }

    // Any OTHER seat left without a provider stays that way, and that is the
    // point: GameEngine::start() rejects it by name, on the game thread, and the
    // run slot's catch-all turns that into gameFailed. A caller who forgot a bot
    // gets told so, rather than having the human quietly play that seat too.

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
        //
        // RAISED ON THE BRIDGE, not emitted here, even though the signal the GUI
        // hears is this object's. This runs on the game thread, and routing it
        // through the bridge is what keeps that one hop the ONLY one - so the
        // event lands in this object's queue, where prepare() can purge it, and
        // game one's failure is never reported over game two.
        gameBridge->raiseGameFailed(QString::fromUtf8(e.what()));
    }
    catch(...)
    {
        gameBridge->raiseGameFailed(QStringLiteral("the game thread failed"));
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

void GameController::discardQueuedSignals()
{
    // Qt purges a posted QMetaCallEvent when its RECEIVER is destroyed, never
    // when its sender is. Destroying a game's bridge therefore purges nothing,
    // and the backlog is not a rare case: stop() blocks the GUI thread in
    // thread.wait() while the game thread publishes to the last card, so
    // anything pressed over the standings arrives with a whole game queued.
    // Measured: 618 of 618 snapshots plus the gameOver, all delivered after game
    // two had started, repainting game one's final table over game two's deal.
    //
    // AFTER stop() AND ONLY AFTER IT - see the header. The join is what makes
    // this sufficient rather than merely helpful: nothing can post behind the
    // purge once the game thread is gone.
    //
    // MetaCall ONLY. DeferredDelete is a different event type and must survive;
    // so must anything a future caller queues at this object for its own sake,
    // which is why this names an event type rather than passing 0.
    //
    // ~GameController needs no equivalent: ~QObject already purges its own.
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
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

void GameController::setPaused(bool paused)
{
    if(gameBridge)
        gameBridge->setPaused(paused);
}

bool GameController::isStopRequested() const
{
    std::lock_guard lock(engineMutex);
    return stopRequested;
}
