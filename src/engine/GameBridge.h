#ifndef GAME_BRIDGE_H
#define GAME_BRIDGE_H

#include "Prompts.h"

#include "core/GameSnapshot.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/RoundType.h>
#include <romanian_whist/Seat.h>

#include <QMetaType>
#include <QObject>
#include <QString>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// This client's own pacing knobs. The terminal's Pacer (src/Pacer.h) is a class
// with beat()/trickPause()/roundPause() and a skip flag, not an options struct -
// the delays are the part worth copying, not the type.
//
// The spectator split is not a detail: phase A IS a spectator game, and 700ms a
// card makes a full 26-round schedule a very long sit. Without it the spectator
// number is declared and never reached.
struct Pacing
{
    std::chrono::milliseconds beat{700};
    std::chrono::milliseconds spectatorBeat{250};
    std::chrono::milliseconds trickDwell{1200};
};

// The observer. LIVES ON THE GAME THREAD - every IGameObserver method below runs
// there, and so does publish().
//
// Modelled on the terminal's TerminalRomanianWhist: same callbacks, same order,
// same reasoning. It publishes a snapshot instead of drawing a frame.
class GameBridge : public QObject, public romanian_whist::IGameObserver
{
    Q_OBJECT

public:
    // std::optional, because a spectator game has no human seat and Seat has no
    // way to say so - no default constructor, no sentinel value.
    explicit GameBridge(std::optional<romanian_whist::Seat> _humanSeat, Pacing _pacing);

    // ---- IGameObserver: all called on the GAME thread ----
    // The engine declares thirteen callbacks and this overrides all thirteen.
    // They are non-pure with empty default bodies, so one left out is silent.

    // Fires while the engine is still IDLE - set up but NOT YET DEALT. So this
    // first snapshot has no trump and no card in any hand.
    //
    // Note where it fires from: the last thing GameEngine::start() does, not the
    // first thing run() does. A MainWindow that connects after the game thread
    // has started misses it outright, which is why GameController splits
    // prepare() from start().
    void onGameStarted(const romanian_whist::GameEngine& engine) override;

    void onRoundStarted(const romanian_whist::GameEngine& engine) override;
    void onBetRequested(const romanian_whist::GameEngine& engine, romanian_whist::Seat seat) override;
    void onBetPlaced(const romanian_whist::GameEngine& engine,
                     romanian_whist::Seat seat,
                     unsigned int bet) override;
    void onBettingComplete(const romanian_whist::GameEngine& engine) override;
    void onTrickStarted(const romanian_whist::GameEngine& engine,
                        unsigned int trickNumber,
                        romanian_whist::Seat leader) override;
    void onCardRequested(const romanian_whist::GameEngine& engine, romanian_whist::Seat seat) override;
    void onCardPlayed(const romanian_whist::GameEngine& engine,
                      romanian_whist::Seat seat,
                      const romanian_whist::Card& card) override;
    void onTrickWon(const romanian_whist::GameEngine& engine,
                    romanian_whist::Seat seat,
                    unsigned int trickNumber) override;
    void onRoundScored(const romanian_whist::GameEngine& engine) override;
    void onRoundComplete(const romanian_whist::GameEngine& engine) override;
    void onGameOver(const romanian_whist::GameEngine& engine) override;
    void onGameStopped(const romanian_whist::GameEngine& engine) override;

signals:
    // BY SHARED POINTER, not by value. Twelve of the thirteen callbacks publish
    // a whole snapshot - six names, six hands, the table - and this makes the
    // queued copy a refcount bump. More usefully it makes it structurally
    // impossible for two threads to hold the same MUTABLE snapshot.
    void stateChanged(std::shared_ptr<const GameSnapshot> snapshot);

    // unsigned int, never Seat: Seat has no default constructor and so can never
    // be a queued signal payload.
    void turnChanged(unsigned int seat);
    void betPlaced(unsigned int seat, unsigned int bet);
    void trickStarted(unsigned int trickNumber, unsigned int leader);
    void cardPlayed(unsigned int seat, romanian_whist::Card card);
    void trickWon(unsigned int seat, unsigned int trickNumber);
    void roundScored();
    void gameOver(std::vector<romanian_whist::Standing> standings,
                  std::vector<romanian_whist::Standing> winners);
    void gameStopped();

    // Raised by QtMoveProvider through the forwarders below, since it has no
    // signals of its own.
    void betRequested(BetPrompt prompt);
    void cardRequested(PlayPrompt prompt);

    // The run slot caught something. NEITHER onGameStopped() NOR onGameOver()
    // fires on that path, so this is the only way the GUI learns the game ended
    // - and without it the exception escapes a slot invoked from QThread's event
    // loop, which is std::terminate with no message.
    //
    // NOT an IGameObserver callback: no engine reports its own failure, and this
    // is raised by GameController's catch-alls rather than by the engine. It
    // lives here anyway, and that placement is the fix for a real bug rather
    // than tidiness - see raiseGameFailed() below.
    void gameFailed(QString what);

public:
    // The seam QtMoveProvider raises its prompts through. Both are called on the
    // GAME thread, from inside the provider.
    //
    // `emit provider->bridge->betRequested(...)` WOULD compile - signals have
    // been public since Qt 5. These exist by convention, not because a compiler
    // enforces them: a signal is an announcement its owner makes, and a
    // forwarder is somewhere to say which thread the call arrives on.
    void raiseBetRequested(BetPrompt prompt) { emit betRequested(std::move(prompt)); }
    void raiseCardRequested(PlayPrompt prompt) { emit cardRequested(std::move(prompt)); }

    // Same seam, same thread, a different game-thread caller: GameController's
    // run slot, from its catch-alls.
    //
    // ROUTED THROUGH HERE rather than emitted by the controller directly, and
    // that is what makes the rule uniform: THE CONTROLLER EMITS NOTHING FROM THE
    // GAME THREAD ITSELF. Everything the GUI hears crosses the thread boundary
    // exactly once, into the controller, which is the object prepare() purges.
    // A gameFailed emitted by the controller would instead post straight to the
    // GUI's receivers, where that purge cannot reach it - and a failure from
    // game one would then be reported over game two.
    void raiseGameFailed(QString what) { emit gameFailed(std::move(what)); }

    // Read by QtMoveProvider, on the GAME thread. PlayContext carries no round
    // type the way BetContext does, so it is cached on the way past in
    // onRoundStarted(). Written there and read here, both on the game thread, so
    // it needs no locking.
    romanian_whist::RoundType currentRoundType() const { return roundType; }

    // Called on the GUI thread, from GameController::stop(). Like the provider's
    // abandon() it touches only a mutex and a condition variable, which is what
    // makes it safe to call on an object that lives on another thread.
    void cancelPacing();

    // HOLDS THE GAME THREAD IN pace() FOR AS LONG AS IT IS SET. Called on the
    // GUI thread, from GameController::setPaused(), and a plain method rather
    // than a slot for the reason section 5 gives: the game thread is inside
    // run() for the whole game, so its event loop never spins and no slot call
    // could ever reach it. Same shape as cancelPacing() and the provider's
    // submit() - a mutex, a flag and a notify.
    //
    // THIS IS THE FIRST INDEFINITE WAIT IN THE CLIENT, which section 15.3 flags
    // as the one thing that adds a hazard to section 6: a pace() with a timeout
    // ends on its own even if a close is dropped, and one waiting on a flag does
    // not. requestStop() cannot reach it - the engine reads that at boundaries
    // and the thread is not at one - so ONLY cancelPacing() can, and stop()
    // calling it unconditionally is what makes a close land while paused. That
    // call stops being tidy and becomes load-bearing here.
    //
    // Nothing clears this between games: prepare() builds a fresh bridge.
    void setPaused(bool paused);

private:
    // Snapshots on the GAME thread and emits the result by value. The GUI thread
    // must never reach into the engine, which has no thread safety at all.
    void publish(const romanian_whist::GameEngine& engine);

    // An INTERRUPTIBLE wait, and one that RETURNS on cancellation rather than
    // throwing. That distinction is the whole of the shutdown story: returning
    // puts the game thread back in run() within microseconds, where it reaches
    // its next boundary, reads the stop flag and ends cleanly through
    // onGameStopped().
    //
    // If this threw instead, the clean path would almost never run - a spectator
    // game spends nearly all its wall-clock time in here.
    //
    // TWO WAITS SINCE D1, not one: the timed beat, and then an indefinite hold
    // while `paused`. See setPaused() above for why the second one is the
    // dangerous half.
    void pace(std::chrono::milliseconds delay);

    // Absent in a spectator game, which makes both helpers one-liners rather
    // than a special case sprinkled through the callbacks.
    bool isHuman(romanian_whist::Seat seat) const
    {
        return humanSeat && seat == *humanSeat;
    }

    std::chrono::milliseconds botBeat() const
    {
        return humanSeat ? pacing.beat : pacing.spectatorBeat;
    }

    std::optional<romanian_whist::Seat> humanSeat;
    Pacing pacing;

    // Game thread only: written in onRoundStarted, read by the provider.
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;

    // No activeSeat member here on purpose. refreshFromEngine() leaves the field
    // to the caller, but the engine does model whose turn it is -
    // GameEngine::getActiveSeat() - with the same "empty between turns" meaning
    // the snapshot's -1 has, so publish() reads it from there. Tracking a second
    // copy across seven callbacks is a state machine that can only ever drift
    // away from the one the engine already keeps.

    // Woken by cancelPacing() so a close does not wait out a trick dwell, and by
    // setPaused() so a resume does not wait out a pause that is already over.
    std::mutex pacingMutex;
    std::condition_variable pacingCv;
    bool pacingCancelled = false;

    // The indefinite half of pace(). Guarded by pacingMutex like the flag above
    // it, and read ONLY through the predicate - a bare bool read outside the
    // lock is the version of this that works until it does not.
    bool pacingPaused = false;
};

// Qt 6 builds a QMetaType from any complete type, so a queued connection usually
// resolves these with the macro absent and omitting it is not a compile error.
// Declared anyway: wherever auto-registration does not reach, the failure is a
// silent "cannot queue argument of type X" on stderr and a signal that never
// arrives. Two lines against a dropped event with no crash to point at.
//
// BetPrompt and PlayPrompt are NOT here: they are declared in Prompts.h, beside
// the structs themselves, so that a click handler including only that header
// sees the same QMetaTypeId specialization this one does.
Q_DECLARE_METATYPE(std::shared_ptr<const GameSnapshot>)
Q_DECLARE_METATYPE(romanian_whist::Card)

// Easy to miss, because it is the only payload that is not one of this client's
// own types: gameOver() carries TWO of these across the boundary.
Q_DECLARE_METATYPE(std::vector<romanian_whist::Standing>)

#endif
