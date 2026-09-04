#include "GameBridge.h"

#include "core/Snapshot.h"

using romanian_whist::Card;
using romanian_whist::GameEngine;
using romanian_whist::Seat;

GameBridge::GameBridge(std::optional<Seat> _humanSeat, Pacing _pacing)
    : humanSeat(_humanSeat), pacing(_pacing)
{}

void GameBridge::publish(const GameEngine& engine)
{
    // Snapshotted HERE, on the game thread.
    GameSnapshot snapshot;

    // BEFORE the refresh, not after. refreshFromEngine() fills the human's hand
    // only when the snapshot already names a human seat, and fills each other
    // seat's hand only for a seat this one can see - both are canSeeHand()
    // questions, and canSeeHand() needs a viewer.
    //
    // Leave it at its default and every snapshot arrives with an empty hand, the
    // table renders correctly, and the player never sees a card.
    //
    // -1 in a spectator game, which is the default and the correct answer.
    snapshot.humanSeat = humanSeat ? static_cast<int>(humanSeat->index) : -1;

    refreshFromEngine(snapshot, engine);

    // Wrapped only on the way out - built by value above, so GameSnapshot's rule
    // that every member is copyable and default-constructible is unchanged.
    emit stateChanged(std::make_shared<const GameSnapshot>(std::move(snapshot)));
}

void GameBridge::onGameStarted(const GameEngine& engine)
{
    publish(engine);
}

void GameBridge::onRoundStarted(const GameEngine& engine)
{
    // Cached on the way past: PlayContext does not carry a round type the way
    // BetContext does, and QtMoveProvider needs it to know whether this round's
    // hand may be shown at all.
    roundType = engine.getCurrentRoundType();

    publish(engine);
}

void GameBridge::onBetRequested(const GameEngine& engine, Seat seat)
{
    // Whose turn it is to bid. Drives the "waiting on Ana" indicator; the human
    // seat's own prompt comes from QtMoveProvider, not from here.
    publish(engine);
    emit turnChanged(seat.index);
}

void GameBridge::onBetPlaced(const GameEngine& engine, Seat seat, unsigned int bet)
{
    publish(engine);
    emit betPlaced(seat.index, bet);

    if(!isHuman(seat))
        pace(botBeat());
}

void GameBridge::onBettingComplete(const GameEngine& engine)
{
    // Hold on the completed bid table before the first card goes down. Without
    // this the last bid is only ever seen when a bot made it.
    publish(engine);
    pace(botBeat());

    // If this client ever grows a skip key, resetSkip() belongs HERE and not in
    // onRoundStarted - the terminal found that out the hard way. A skip runs to
    // the end of a round, so it has to survive the bidding beats above.
}

void GameBridge::onTrickStarted(const GameEngine& engine, unsigned int trickNumber, Seat leader)
{
    publish(engine);
    emit trickStarted(trickNumber, leader.index);
}

void GameBridge::onCardRequested(const GameEngine& engine, Seat seat)
{
    publish(engine);
    emit turnChanged(seat.index);
}

void GameBridge::onCardPlayed(const GameEngine& engine, Seat seat, const Card& card)
{
    publish(engine);
    emit cardPlayed(seat.index, card);

    // The human's own card needs no pause: the time they spent choosing it
    // already served as one.
    if(!isHuman(seat))
        pace(botBeat());
}

void GameBridge::onTrickWon(const GameEngine& engine, Seat seat, unsigned int trickNumber)
{
    // The finished trick is still the engine's CURRENT one throughout this
    // callback, so the snapshot carries the completed table rather than a blank
    // one. Note refreshFromEngine() reads the seats off that trick rather than
    // recomputing them from the leader, which has already moved to the winner.
    publish(engine);
    emit trickWon(seat.index, trickNumber);
    pace(pacing.trickDwell);
}

void GameBridge::onRoundScored(const GameEngine& engine)
{
    // This callback and not onRoundComplete: it is the last one at which the
    // engine still names the round just scored, so a round sheet drawn from it
    // shows the right round's numbers.
    publish(engine);
    emit roundScored();
}

void GameBridge::onRoundComplete(const GameEngine& engine)
{
    publish(engine);
    pace(pacing.trickDwell);
}

void GameBridge::onGameOver(const GameEngine& engine)
{
    // Publish first: the final state is what a game-over dialog is drawn over.
    publish(engine);

    // getWinners() rather than standings.front() - a drawn game has more than
    // one winner, and the engine is what decides that. Reading the first row
    // quietly names one winner of a game two seats drew, and that bug shipped
    // once already in the terminal's 2.1.0.
    emit gameOver(engine.getStandings(), engine.getWinners());
}

void GameBridge::onGameStopped(const GameEngine&)
{
    // requestStop() was honoured. THIS, not onGameOver, is what a window closed
    // mid-game produces. The round it landed in is left unscored, so there is
    // nothing to announce and nothing to publish; the window is closing anyway.
    emit gameStopped();
}

void GameBridge::pace(std::chrono::milliseconds delay)
{
    std::unique_lock lock(pacingMutex);
    pacingCv.wait_for(lock, delay, [this]{ return pacingCancelled; });

    // RETURNS on cancel. Does NOT throw - see the header. Getting this backwards
    // makes the clean shutdown path unreachable.
}

void GameBridge::cancelPacing()
{
    {
        std::lock_guard lock(pacingMutex);
        pacingCancelled = true;
    }

    pacingCv.notify_all();
}
