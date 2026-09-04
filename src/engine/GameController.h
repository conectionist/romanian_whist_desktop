#ifndef GAME_CONTROLLER_H
#define GAME_CONTROLLER_H

#include "GameBridge.h"
#include "QtMoveProvider.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/Seat.h>

#include <QObject>
#include <QString>
#include <QThread>

#include <memory>
#include <mutex>
#include <optional>

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
    // hands them back; the caller connects; only then does start() spawn the
    // thread.
    //
    // Doing it in one call cannot work: onGameStarted() fires from inside
    // GameEngine::start(), so the first stateChanged() is emitted before run()
    // is entered, and a MainWindow that connects afterwards has already missed
    // it. A queued connection made after the emit does not receive it.
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

    // GameSetup is MOVE-ONLY - it owns the seats' unique_ptr<IMoveProvider> - so
    // the call site reads start(std::move(setup)), and the setup is moved again
    // into the run slot's captures. It cannot travel through a queued signal,
    // which is why it goes into the lambda rather than a slot argument.
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
    // The caller's GameSetup is gutted by the move whatever happens, so a
    // start() that throws needs a freshly built one to retry - not because the
    // engine kept anything, but because a move-only type passed by value is
    // spent at the call site.
    void start(romanian_whist::GameSetup setup);

    // Ends the game and joins the thread. Safe to call before prepare(), on a
    // game that never started, on one that already finished, and twice.
    void stop();

    // Built by prepare(); connect to it BEFORE start().
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

signals:
    // The run slot caught something. NEITHER onGameStopped() NOR onGameOver()
    // fires on that path, so this signal is the only way the GUI learns the game
    // ended - and without it the exception escapes a slot invoked from QThread's
    // event loop, which is std::terminate with no message.
    void gameFailed(QString what);

private:
    // The slot QThread::started is connected to. Runs the whole game.
    void runGame(romanian_whist::GameSetup setup);

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
    std::mutex engineMutex;
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
