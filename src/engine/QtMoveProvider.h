#ifndef QT_MOVE_PROVIDER_H
#define QT_MOVE_PROVIDER_H

#include "Prompts.h"

#include <romanian_whist/CardValidator.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/Seat.h>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

class GameBridge;

// The blocking seam: the engine asks this for a move on the GAME thread, and it
// parks there until a click on the GUI thread answers.
//
// DELIBERATELY NOT A QObject. The GUI thread calls submit() on it directly -
// that touches only a mutex, so it needs no signal, and skipping Qt's event
// queue means the game thread wakes immediately rather than at the next event
// loop turn.
//
// A prompt handler may therefore answer inline, from inside the betRequested or
// cardRequested emission itself, direct connection and all: makeBet() and
// playCard() drop their lock across the emit precisely so that submit() can take
// it on the emitting thread.
class QtMoveProvider : public romanian_whist::IMoveProvider
{
public:
    QtMoveProvider(GameBridge* _bridge, romanian_whist::Seat _seat);

    // ---- called on the GAME thread. Block until the GUI answers. ----
    unsigned int makeBet(const romanian_whist::BetContext& context) override;
    std::optional<std::size_t> playCard(const romanian_whist::PlayContext& context) override;

    // ---- called on the GUI thread, straight from a click handler ----
    //
    // `forPrompt` is the generation the widget was drawn for, carried out on the
    // prompt and handed back. A submission for any other generation is dropped.
    //
    // Without that gate a click landing when nothing is parked - a stray click
    // during a bot's turn, a double click, a dialog accepted twice - sets the
    // answer, and the NEXT prompt returns immediately with a choice made for a
    // different hand. Disabling the widget between turns narrows that window
    // without closing it.
    void submit(unsigned int forPrompt, std::size_t choice);

    // Wakes a parked provider so it throws GameAbandoned. Safe to call before it
    // has ever parked: this only latches a flag, which the next wait sees.
    void abandon();

private:
    GameBridge* bridge;                       // non-owning; outlives this
    romanian_whist::Seat seat;

    // getLegalCards() is a non-static const member, so the validator has to be
    // an object rather than a call on the class.
    romanian_whist::CardValidator validator;

    std::mutex mutex;
    std::condition_variable cv;

    // One channel for two kinds of answer: a bet, and an index into a hand.
    // They never overlap - the engine asks for one at a time - and the
    // generation is what keeps an answer to one from being read as an answer to
    // the other.
    unsigned int generation = 0;              // bumped per prompt raised
    unsigned int answeredPrompt = 0;          // the generation `answer` answers
    std::optional<std::size_t> answer;
    bool abandoned = false;
};

// What the ENGINE holds. Ten lines, and every one of them exists so that the
// real provider can outlive the engine: GameSetup::seats[i].moveProvider is a
// unique_ptr, so whatever goes in there is destroyed with the engine, on the
// game thread. A click landing while the game-over dialog is up would then find
// an object destroyed on another thread half a second earlier.
//
// So the engine gets this, which owns nothing, and GameController keeps the
// QtMoveProvider itself.
//
// Not a QObject either, and for a sharper reason than the above: this one IS
// destroyed on the game thread, inside ~GameEngine. Destroying a QObject with
// GUI-thread affinity from another thread is a problem you do not want to
// debug.
class SeatProvider : public romanian_whist::IMoveProvider
{
public:
    // Non-owning by construction - there is no unique_ptr overload to reach for
    // by accident, which is the whole point of the type.
    explicit SeatProvider(QtMoveProvider* _target) : target(_target) {}

    unsigned int makeBet(const romanian_whist::BetContext& context) override
    {
        return target->makeBet(context);
    }

    std::optional<std::size_t> playCard(const romanian_whist::PlayContext& context) override
    {
        return target->playCard(context);
    }

private:
    QtMoveProvider* target;    // owned by GameController; outlives the engine
};

#endif
