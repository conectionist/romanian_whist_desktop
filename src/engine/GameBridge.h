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

    // Read by QtMoveProvider, on the GAME thread. PlayContext carries no round
    // type the way BetContext does, so it is cached on the way past in
    // onRoundStarted(). Written there and read here, both on the game thread, so
    // it needs no locking.
    romanian_whist::RoundType currentRoundType() const { return roundType; }

    // Called on the GUI thread, from GameController::stop(). Like the provider's
    // abandon() it touches only a mutex and a condition variable, which is what
    // makes it safe to call on an object that lives on another thread.
    void cancelPacing();

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

    // Whose turn it is, or -1 for nobody - the encoding GameSnapshot uses, and
    // the reason this is an int rather than a Seat. Snapshot.h names it as one
    // of the two fields refreshFromEngine() leaves to the caller, because the
    // engine does not model it.
    //
    // Set and cleared in the callbacks below and copied into every snapshot, so
    // like roundType it is game-thread state and needs no locking. turnChanged()
    // cannot stand in for it: it announces a turn starting and has no way to say
    // that nobody is on turn.
    int activeSeat = -1;

    // Woken by cancelPacing() so a close does not wait out a trick dwell.
    std::mutex pacingMutex;
    std::condition_variable pacingCv;
    bool pacingCancelled = false;
};

// Qt 6 builds a QMetaType from any complete type, so a queued connection usually
// resolves these with the macro absent and omitting it is not a compile error.
// Declared anyway: wherever auto-registration does not reach, the failure is a
// silent "cannot queue argument of type X" on stderr and a signal that never
// arrives. Two lines against a dropped event with no crash to point at.
Q_DECLARE_METATYPE(std::shared_ptr<const GameSnapshot>)
Q_DECLARE_METATYPE(BetPrompt)
Q_DECLARE_METATYPE(PlayPrompt)
Q_DECLARE_METATYPE(romanian_whist::Card)

// Easy to miss, because it is the only payload that is not one of this client's
// own types: gameOver() carries TWO of these across the boundary.
Q_DECLARE_METATYPE(std::vector<romanian_whist::Standing>)

#endif
