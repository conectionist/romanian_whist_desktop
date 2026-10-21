#ifndef GAME_CONTROLLER_H
#define GAME_CONTROLLER_H

#include "GameBridge.h"
#include "QtMoveProvider.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/Seat.h>

#include <QObject>
#include <QString>
#include <QThread>

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// A GameSetup and the observers that have to be registered with the engine
// BEFORE it starts. ONE TYPE, because the two are one fact: every pointer in
// `observers` aims at an object `setup` itself owns, so a caller who moves one
// without the other has either a strategy playing blind or a dangling pointer.
//
// WHY IT EXISTS AT ALL. ReckonerStrategy and CyborgStrategy are each an
// IStrategy *and* an IGameObserver - each plays out of a card-counting memory
// that only the observer callbacks fill - and GameEngine::addObserver() is
// neither safe off the game thread nor useful after start(), which fires
// onGameStarted() as its last act. The seats are built on the GUI thread, where
// no engine exists; the registration has to happen on the game thread between
// the engine's construction and its start(). This is what carries the pointers
// across, and it is the arrangement that does NOT also move seat construction
// over the boundary - start() still fills the human's seat by index, on the GUI
// thread, where .at() can throw at the caller.
//
// LIFETIME IS CORRECT BY CONSTRUCTION, which is why there is no removeObserver()
// anywhere. runGame() takes this BY VALUE, so it is constructed before the local
// GameEngine and destroyed after it: before start() the strategies are owned by
// `setup` and outlive the engine, and after a successful start() they are owned
// by the engine's Players and die WITH it, taking this list of pointers into them
// along. GameEngine::start() validates everything before it moves anything, so
// its own refusals leave them in `setup` too.
//
// ONE THROW COMES AFTER THE MOVE, and it is an observer's rather than the
// engine's: start() fires onGameStarted() as its last act, and CyborgStrategy
// throws from there when its name matches no seat. By then the strategies are
// the engine's Players', so they die with the engine local - still before this
// deal - and the pointers left in `observers` are never read again. So the
// conclusion holds on every path: no observer outlives the engine it was
// registered with.
//
// NOT A GameController MEMBER, and that is the same decision as stopRequested's.
// A member survives prepare(), which is where "play again" joins and rebuilds -
// so a list left behind points into game one's destroyed engine and is read by
// game two's run slot. It travels with the setup and dies with it instead.
struct GameDeal
{
    romanian_whist::GameSetup setup;
    std::vector<romanian_whist::IGameObserver*> observers;

    // Load-bearing: the converting constructor below suppresses the implicit
    // default one, and the tests default-construct.
    GameDeal() = default;

    // NOT EXPLICIT, for the table with no watching strategy on it - which is
    // every fixture and every call site that predates this type. The conversion
    // can only ADD an empty observer list, never lose a filled one; losing one
    // takes naming the member, which is what the negative half of the
    // registration test does on purpose.
    GameDeal(romanian_whist::GameSetup _setup) : setup(std::move(_setup)) {}
};

// Owns the game thread.
//
// The engine is constructed, played and destroyed entirely on that thread. This
// is not a preference: the engine's README opens "Threading, stopping, and
// failure" with ONE THREAD PER ENGINE AND NO INTERNAL LOCKING, and
// requestStop() is the only member of the whole class safe to touch from
// anywhere else - addObserver() and removeObserver() explicitly are not.
class GameController : public QObject
{
    Q_OBJECT

public:
    explicit GameController(QObject* parent = nullptr);

    // Calls stop(). Not a tidiness measure: `gameBridge` is a QObject with
    // affinity to the game thread, and destroying it from the GUI thread is only
    // defined once that thread has been joined. stop() is what joins it, and it
    // is idempotent and safe on a game that never started or has already
    // finished - so calling it unconditionally here costs nothing and closes the
    // one path (a controller destroyed without a window close) that would
    // otherwise skip it.
    //
    // In the BODY, so the join happens before any member is destroyed.
    //
    // This is the mirror of why QtMoveProvider is not a QObject at all: same
    // hazard, opposite thread.
    ~GameController() override;

    // TWO calls, not one, and the split is load-bearing. prepare() builds the
    // bridge - and a provider, if there is a human seat - on the GUI thread and
    // relays its signals to this object's mirrors; only then does start() spawn
    // the thread.
    //
    // Doing it in one call cannot work: onGameStarted() fires from inside
    // GameEngine::start(), so the first stateChanged() is emitted before run()
    // is entered. The relay has to be in place by then, and a relay made after
    // the emit does not receive it.
    //
    // THE GUI DOES NOT CONNECT HERE. It connects to this object's signals once,
    // before the first prepare(), and never again - see the signals block.
    //
    // A SPECTATOR GAME GETS NO PROVIDER: there is no human seat for one to
    // serve. That is not an edge case to bolt on later - phase A is a spectator
    // game, so a null provider is the first state this class is ever in, and
    // stop() null-checks it for exactly that reason.
    //
    // THE JOIN POINT FOR REUSE, and it performs that join rather than assuming
    // one has happened: prepare() calls stop() first. A "play again" pressed
    // over the standings arrives while the run slot is still inside
    // onGameOver(), so without it this call frees the bridge that thread is
    // publishing through, and the start() that follows returns silently on a
    // QThread that is still running - a second game that never runs at all.
    //
    // That is also what makes the rest of this safe. Clears stopRequested, under
    // engineMutex, after the stop and before it builds anything: the previous
    // game's stop set it, leaving it set stops the next one before it starts,
    // and by then there is no run slot left to race.
    void prepare(std::optional<romanian_whist::Seat> humanSeat, Pacing pacing);

    // GameDeal is MOVE-ONLY - its setup owns the seats' unique_ptr<IMoveProvider>
    // - so the call site reads start(std::move(deal)), and the deal is moved
    // again into the run slot's captures. It cannot travel through a queued
    // signal, which is why it goes into the lambda rather than a slot argument.
    //
    // A BARE GameSetup STILL CONVERTS, which is how every caller with no watching
    // strategy reads unchanged. See GameDeal above for what the observers are and
    // why they cannot be registered here.
    //
    // REQUIRES AN IDLE THREAD, which prepare() is what provides. Asserted rather
    // than repaired here: a start() that quietly ended the running game would be
    // making stop()'s decision for the caller.
    //
    // The human seat's moveProvider is left NULL by the caller and filled in
    // here with a SeatProvider - the non-owning forwarder, NOT the
    // QtMoveProvider itself, which this class keeps. GameEngine::start() rejects
    // a null provider, so the seat cannot simply be left empty.
    //
    // The seat filled is the one prepare() was given, addressed by index. Any
    // OTHER empty seat is left for the engine to reject by name: a caller who
    // forgot a bot is told so, rather than having the human play that seat too.
    //
    // The caller's GameDeal is gutted by the move whatever happens, so a
    // start() that throws needs a freshly built one to retry - not because the
    // engine kept anything, but because a move-only type passed by value is
    // spent at the call site.
    void start(GameDeal deal);

    // Ends the game and joins the thread. Safe to call before prepare(), on a
    // game that never started, on one that already finished, and twice.
    void stop();

    // THE DEAD GAME'S UNDELIVERED SIGNALS, dropped on the floor. Call it AFTER
    // stop(), never instead of one: stop() joins, which is what makes this
    // sufficient rather than merely helpful - nothing can post behind the purge
    // once the game thread is gone.
    //
    // NOT PART OF stop(), and that is the sharp edge. A stop is also the
    // ORDINARY end of a game the player is still reading, where the queue holds
    // the standings the window has yet to draw; purging there deletes the
    // ending. Only a caller who knows the old game's output is worthless can ask
    // for this - prepare(), which is about to replace everything, and a window
    // abandoning a game to the main menu.
    void discardQueuedSignals();

    // HOLDS THE GAME WHERE IT IS. Forwards to GameBridge::setPaused(); see there
    // for why this is a plain call rather than a signal, and for the hazard it
    // introduces.
    //
    // NULL-SAFE, on the same reasoning as stop()'s two guards: the bridge is
    // built by prepare(), so a pause asked for before a game exists has nobody
    // to tell. It is a no-op rather than an error because the caller is a menu
    // and a menu is reachable with no game running.
    //
    // The pause lands within one pacing beat, and NOT AT ALL while the game
    // thread is parked in a prompt waiting for the player - which is already a
    // pause, so there is nothing there to do.
    void setPaused(bool paused);

    // NEW DELAYS FOR THE GAME IN PLAY, from the menu bar's Settings. Forwards to
    // GameBridge::setPacing(); see there. A no-op with no game, like setPaused():
    // the next game takes its pacing from prepare().
    void setPacing(Pacing pacing);

    // Built by prepare(), destroyed by the next one. NOT the GUI's seam: GUI code
    // connects to this object's mirrored signals instead, which outlive a game -
    // see the signals block for what connecting here across a "play again" costs.
    //
    // Public for the two callers that genuinely want the per-game object on the
    // game thread: QtMoveProvider, which raises its prompts through it, and the
    // tests, which connect DIRECT to run assertions on the game thread itself.
    GameBridge* bridge() const { return gameBridge.get(); }

    // NULL in a spectator game - see prepare(). Every caller checks.
    //
    // Owned HERE, not by the engine, and that is a lifetime fix rather than a
    // style choice. The engine owns whatever is in GameSetup and destroys it
    // with itself on the game thread - so a provider owned by the engine is a
    // dangling pointer the moment run() returns on its own, and a click that
    // arrives while the game-over dialog is up is a use-after-free that no
    // amount of stopping protects against.
    QtMoveProvider* provider() const { return movePlayer.get(); }

    // Has a stop been asked for in the game currently set up? The same latch
    // stop() raises and the run slot reads back, read under the same mutex.
    //
    // EXISTS BECAUSE THE ORDERING IS OTHERWISE UNOBSERVABLE. The test for a stop
    // landing before the engine is published has to release the parked game
    // thread only once the latch is genuinely set, and every other effect of
    // stop() is either on an object with no state to read (cancelPacing(),
    // abandon()) or on one that does not exist yet (the engine). Spinning on a
    // flag the stopping thread sets BEFORE it calls stop() proves nothing: the
    // release can beat the latch, and the test then passes by exercising the
    // ordinary post-publish path it was written to avoid.
    //
    // Neither this nor stop() blocks while holding engineMutex, so a caller
    // spinning on this cannot wedge the thread it is watching.
    bool isStopRequested() const;

signals:
    // WHAT THE GUI CONNECTS TO. Every one of these is a mirror of the identically
    // named GameBridge signal, relayed by prepare().
    //
    // The bridge does not survive a game and this object does, and that
    // difference is the whole reason these exist. Qt purges a posted
    // QMetaCallEvent when its RECEIVER is destroyed, never when its sender is -
    // so destroying game one's bridge in prepare() leaves whatever the GUI
    // connected to it holding game one's entire undelivered backlog. prepare()
    // makes that backlog certain rather than unlikely, because it blocks the GUI
    // thread in thread.wait() while the game thread runs on. Measured on a real
    // game: 618 of 618 stateChanged events plus the gameOver arrived AFTER game
    // two had started - a "play again" that repaints game one's final table over
    // game two's deal and re-pops the standings over it.
    //
    // Relaying through this object fixes it because this object is the receiver.
    // prepare() purges its own queue, and one statement there closes the whole
    // class of bug.
    //
    // So: CONNECT TO THESE ONCE, EVER - in the window's constructor, before any
    // prepare(). There is no per-game reconnection step, which is the second
    // half of the point: a step that must be repeated after every prepare() is a
    // step A5 can forget, and forgetting it silently shows a dead first frame.
    //
    // Parameter types are spelled exactly as GameBridge spells them, because moc
    // normalises the names and a queued relay looks up the metatype by that
    // name. Spell one differently and the hop fails at runtime with a line on
    // stderr and no crash.
    void stateChanged(std::shared_ptr<const GameSnapshot> snapshot);
    void turnChanged(unsigned int seat);
    void betPlaced(unsigned int seat, unsigned int bet);
    void trickStarted(unsigned int trickNumber, unsigned int leader);
    void cardPlayed(unsigned int seat, romanian_whist::Card card);
    void trickWon(unsigned int seat, unsigned int trickNumber);
    void roundScored(RoundResult result);
    void gameOver(std::vector<romanian_whist::Standing> standings,
                  std::vector<romanian_whist::Standing> winners);
    void gameStopped();
    void betRequested(BetPrompt prompt);
    void cardRequested(PlayPrompt prompt);

    // The run slot caught something. NEITHER onGameStopped() NOR onGameOver()
    // fires on that path, so this signal is the only way the GUI learns the game
    // ended - and without it the exception escapes a slot invoked from QThread's
    // event loop, which is std::terminate with no message.
    //
    // RELAYED like the rest, not emitted here. The run slot raises it on the
    // bridge - see GameBridge::raiseGameFailed() for why that indirection is
    // load-bearing rather than decorative.
    void gameFailed(QString what);

private:
    // The slot QThread::started is connected to. Runs the whole game.
    //
    // BY VALUE, and that is the lifetime argument in GameDeal's comment rather
    // than a convention: a by-value parameter is constructed before the first
    // local and destroyed after it, so the deal outlives the engine local below
    // on every path out of this function.
    void runGame(GameDeal deal);

    QThread thread;

    // The QThread::started connection, kept so start() can replace it rather
    // than add to it. A controller is reused for a second game (phase D), and
    // connect() appends: a second start() would otherwise leave TWO run slots
    // attached, both firing on the next thread.start(), both constructing an
    // engine, both driving the same bridge.
    QMetaObject::Connection runConnection;

    std::unique_ptr<GameBridge> gameBridge;       // built on the GUI thread, then moveToThread()
    std::unique_ptr<QtMoveProvider> movePlayer;   // null in a spectator game

    // WHICH seat that provider serves, kept because start() has no other way to
    // know. The same optional GameBridge was handed, set beside movePlayer and
    // engaged on exactly the same condition - so the seat the player drives and
    // the seat a snapshot marks as theirs cannot drift apart.
    std::optional<romanian_whist::Seat> humanPlayerSeat;

    // The engine is constructed, used and destroyed on the game thread, so the
    // GUI thread may not read this pointer unguarded - stop() would otherwise
    // race the run slot's construction (and find it null if the window is closed
    // in the gap between thread.start() and the slot running) and its
    // destruction. Published under the mutex once built, withdrawn under it
    // before destruction, and stop() calls requestStop() while holding it.
    // Cheap: the game thread takes this lock exactly twice per game.
    //
    // Note there is no GameEngine MEMBER, so section 21.2's "declare the engine
    // last" has nothing to apply to here: the engine is a local in runGame(),
    // born and destroyed inside the slot, and this is only a borrowed pointer to
    // it that is withdrawn before it dies.
    // MUTABLE so isStopRequested() can be const. Locking a mutex to read state
    // is not a modification of the object as callers see it.
    mutable std::mutex engineMutex;
    romanian_whist::GameEngine* engine = nullptr;

    // The stop that arrived before there was an engine to tell. Guarding the
    // pointer stops the crash and loses the REQUEST: the engine's own stop flag
    // is a member of an object that does not exist yet, so there is nothing for
    // stop() to set and nothing to re-read later. Latched here instead, under
    // the same mutex, and honoured by the run slot the moment it publishes the
    // pointer.
    //
    // CLEARED BY prepare(), and that is not housekeeping. A controller is reused
    // for a second game (phase D), and the stop that ended the first one set
    // this - so a flag left latched is read back by game two's run slot, which
    // requests a stop before the first card is dealt and ends it at the first
    // boundary: a game that appears to deal itself and stop.
    bool stopRequested = false;
};

#endif
