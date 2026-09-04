#include "QtMoveProvider.h"

#include "GameAbandoned.h"
#include "GameBridge.h"

#include "core/HandLayout.h"

#include <algorithm>
#include <utility>
#include <vector>

using romanian_whist::BetContext;
using romanian_whist::Card;
using romanian_whist::PlayContext;
using romanian_whist::RoundType;

QtMoveProvider::QtMoveProvider(GameBridge* _bridge) : bridge(_bridge)
{}

unsigned int QtMoveProvider::makeBet(const BetContext& context)
{
    // roundType matters here beyond presentation. In Forehead and Hidden the
    // bidder is not entitled to their own hand's contents - the engine still
    // passes the real hand, having no other to give - so the prompt must not
    // show it back to them.
    //
    // Unlike playCard(), THIS context carries the round type, so nothing has to
    // be cached to know it.
    const bool blind = context.roundType != RoundType::Normal;

    // Laid out rather than copied raw, and laid out with no legal set - which is
    // what says "nothing is being chosen here", so every card reads legal and
    // the bidding screen greys nothing. Same display order the play prompt will
    // use, so cards do not rearrange themselves between bidding and playing.
    //
    // Copied out of the context here, before the wait: BetContext::hand is a
    // const reference and the context does not outlive this call.
    std::vector<HandEntry> shown;
    if(!blind)
        shown = layOutHand(context.hand, {}, context.trump).entries;

    std::unique_lock lock(mutex);

    // Asks again rather than trusting the answer. The prompt carries everything
    // needed to build a widget that cannot produce an illegal bet, so a bad one
    // arriving here is a UI bug - but the ENGINE's response to a bad one is to
    // throw out of run(), which reaches the player as "the game thread failed"
    // with the game unresumable. Re-prompting costs nothing when the UI is right
    // and is the whole fix when it is not.
    while(true)
    {
        // BEFORE the prompt goes out, not only after the wait. abandon() only
        // latches a flag, so it may already be set the first time through -
        // raising first tells a GUI that is tearing down to ask for a bid, on
        // the way to throwing out from under it.
        if(abandoned)
            throw GameAbandoned{};

        const unsigned int prompt = ++generation;

        // UNLOCKED ACROSS THE EMIT, and locked again before the wait.
        //
        // In this client the signal crosses threads and is queued, so the
        // handler runs later on the GUI thread and the lock would not matter.
        // A DIRECT connection runs it right here instead, on this thread - and
        // a handler that answers inline calls submit(), which takes this same
        // NON-RECURSIVE mutex. Emitting while holding it is then a self-deadlock
        // that parks the game thread for good: abandon() cannot get through
        // either, so closing the window hangs on the join with nothing to show
        // for it. Not a hypothetical connection - QObject::connect's 3-argument
        // functor overload is direct by definition, and the tests use it.
        //
        // Nothing is lost by the gap. `generation` is already bumped, so a
        // submit() landing inside it is accepted rather than dropped as stale,
        // and it sets answeredPrompt before notifying; wait() evaluates its
        // predicate on entry, so an answer - or an abandon - that arrives while
        // the lock is down is seen rather than missed.
        lock.unlock();

        // COPIED, not moved: the loop may raise this prompt again.
        bridge->raiseBetRequested(BetPrompt{ prompt,
                                             context.hand.size(),
                                             context.forbiddenBet,
                                             context.roundType,
                                             context.isFirstPlayer,
                                             shown });

        lock.lock();

        cv.wait(lock, [this, prompt]{ return answeredPrompt == prompt || abandoned; });

        // Still needed, and not made redundant by the check above: the wait
        // returns on an abandon with `answer` empty, and falling through would
        // dereference an empty optional.
        if(abandoned)
            throw GameAbandoned{};

        // A bet, not a hand index - one channel carries both, and the
        // generation is what keeps them apart.
        //
        // std::exchange clears it. Leaving it set means the next prompt returns
        // this same value without ever waiting.
        //
        // Kept as a size_t until it has been checked. Narrowing first makes a
        // submitted 2^32+1 wrap to a legal bet of 1 and reach the engine, where
        // re-prompting was the whole point; playCard() checks the untruncated
        // value, and both take the same untrusted number from the same submit().
        const std::size_t submitted = *std::exchange(answer, std::nullopt);

        // GameEngine::isBetLegal() is these two rules and nothing else - no more
        // tricks than there are cards, and never the bet that would make the
        // totals add up. Restated rather than called: this holds no engine
        // reference, and the engine is deliberately not reachable from here.
        //
        // Past them the value is at most the hand size, so the narrowing is
        // provably lossless.
        if(submitted <= context.hand.size() &&
           (!context.forbiddenBet || submitted != *context.forbiddenBet))
            return static_cast<unsigned int>(submitted);
    }
}

std::optional<std::size_t> QtMoveProvider::playCard(const PlayContext& context)
{
    const std::vector<Card> legal =
        validator.getLegalCards(context.hand, context.trump, context.leadSuit);

    // No legal play. Matches what the engine's own strategies do; prompting
    // would be impossible since no choice could ever be valid.
    if(legal.empty())
        return std::nullopt;

    // Display order, real legality, and the way back to the engine's indices,
    // built HERE on the game thread out of the hand the engine has just handed
    // over. This is the only point in the program where all three exist at
    // once, which is why the prompt carries them and a snapshot cannot stand in.
    const HandLayout layout = layOutHand(context.hand, legal, context.trump);

    // PlayContext carries NO roundType - BetContext does, this does not - so the
    // bridge cached it in onRoundStarted and it is read back here. It cannot be
    // inferred from the hand size either: a Normal 1-trick round looks
    // identical to a Forehead one.
    const bool blind = bridge->currentRoundType() != RoundType::Normal;

    std::unique_lock lock(mutex);

    // Loops for the reason makeBet() does, and here the legal set to check
    // against is already sitting in a local.
    while(true)
    {
        // Before the prompt goes out, for the reason makeBet() gives.
        if(abandoned)
            throw GameAbandoned{};

        const unsigned int prompt = ++generation;

        // Unlocked across the emit, for the reason makeBet() gives at length.
        lock.unlock();

        bridge->raiseCardRequested(PlayPrompt{ prompt,
                                               blind ? std::vector<HandEntry>{}
                                                     : layout.entries,
                                               layout.positions,
                                               blind });

        lock.lock();

        cv.wait(lock, [this, prompt]{ return answeredPrompt == prompt || abandoned; });

        if(abandoned)
            throw GameAbandoned{};

        // Already an index into context.hand, because the click handler
        // translated through PlayPrompt::handIndex before submitting.
        // IMoveProvider is explicit that this is an index into the hand AS
        // GIVEN, not into any sorted or filtered view of it.
        const std::size_t chosen = *std::exchange(answer, std::nullopt);

        // The prompt marked each slot legal or not and the widget is meant to
        // refuse the rest; this is what runs when it does not. The bounds check
        // is not paranoia either - handIndex is the only sanctioned way to
        // produce this number, and a widget indexing it with a stale display
        // slot produces one that is out of range.
        //
        // By value, because a deck holds no duplicates.
        if(chosen < context.hand.size() &&
           std::find(legal.begin(), legal.end(), context.hand[chosen]) != legal.end())
            return chosen;
    }
}

void QtMoveProvider::submit(unsigned int forPrompt, std::size_t choice)
{
    {
        std::lock_guard lock(mutex);

        if(forPrompt != generation || answeredPrompt == generation)
            return;                      // stale, or already answered

        answer = choice;
        answeredPrompt = forPrompt;
    }

    cv.notify_one();
}

void QtMoveProvider::abandon()
{
    {
        std::lock_guard lock(mutex);
        abandoned = true;
    }

    cv.notify_one();
}
