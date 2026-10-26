#include "GameBridge.h"

#include "core/Snapshot.h"

#include <utility>

using romanian_whist::Card;
using romanian_whist::GameEngine;
using romanian_whist::Seat;

GameBridge::GameBridge(std::optional<Seat> _humanSeat, Pacing _pacing)
    : humanSeat(_humanSeat), pacing(_pacing)
{}

void GameBridge::setProfiles(std::vector<PlayerProfile> seatProfiles)
{
    profiles = std::move(seatProfiles);
}

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

    // The other field refreshFromEngine() leaves to the caller. Unlike humanSeat
    // above, nothing in the refresh reads it, so it could equally go after -
    // it sits here because the two are set for the same reason.
    //
    // ASKED OF THE ENGINE rather than tracked here. getActiveSeat() is engaged
    // from just before a provider is asked until just after its answer is
    // recorded, which is exactly the highlight this drives: it stays on the seat
    // that just bid or just played, and goes empty between turns without any
    // callback having to remember to say so.
    //
    // Safe in onGameStarted(), where the engine is still idle - unlike most of
    // its accessors this one is a plain return with no requireStarted().
    const std::optional<Seat> active = engine.getActiveSeat();
    snapshot.activeSeat = active ? static_cast<int>(active->index) : -1;

    refreshFromEngine(snapshot, engine);

    // AFTER the refresh, which builds the seats from the engine, and outside it:
    // refreshFromEngine() stays a pure function of the engine, as the terminal's
    // is (SnapshotTests). Seat i is profile i (GameDeal::profiles).
    for(std::size_t seat = 0 ; seat < snapshot.seats.size() && seat < profiles.size() ; seat++)
    {
        snapshot.seats[seat].gender = profiles[seat].gender;
        snapshot.seats[seat].avatar = profiles[seat].avatar;
    }

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
    // The snapshot names this seat as active, straight from the engine. Drives
    // the "waiting on Ana" indicator; the human seat's own prompt comes from
    // QtMoveProvider, not from here.
    publish(engine);
    emit turnChanged(seat.index);
}

void GameBridge::onBetPlaced(const GameEngine& engine, Seat seat, unsigned int bet)
{
    // The snapshot still names this seat as active, which is what the engine
    // means by keeping getActiveSeat() engaged until just after the answer is
    // recorded: the frame showing the bid that was just made should still
    // highlight who made it.
    publish(engine);
    emit betPlaced(seat.index, bet);

    if(!isHuman(seat))
        pace(Beat::Bot);
}

void GameBridge::onBettingComplete(const GameEngine& engine)
{
    // Hold on the completed bid table before the first card goes down. Without
    // this the last bid is only ever seen when a bot made it.
    publish(engine);
    pace(Beat::Bot);

    // If this client ever grows a skip key, resetSkip() belongs HERE and not in
    // onRoundStarted - the terminal found that out the hard way. A skip runs to
    // the end of a round, so it has to survive the bidding beats above.
}

void GameBridge::onTrickStarted(const GameEngine& engine, unsigned int trickNumber, Seat leader)
{
    // The leader has not been ASKED yet - onCardRequested is what says that -
    // so this frame has nobody on turn, and the engine says so too.
    publish(engine);
    emit trickStarted(trickNumber, leader.index);
}

void GameBridge::onCardRequested(const GameEngine& engine, Seat seat)
{
    publish(engine);
    emit turnChanged(seat.index);

    // A BEAT BEFORE A BOT'S CARD, so this frame is seen at all. Without it
    // onCardPlayed() publishes microseconds later and supersedes it - measured
    // in phase A's log, the gap was under a millisecond - so the seat on turn
    // never renders and cards appear to teleport onto the table.
    //
    // The terminal does the same, drawing "X is thinking..." and then pacing.
    // Not for the human: their own deliberation is the pause, and pausing
    // before asking them would only delay the prompt.
    //
    // Note this is NOT wanted on onBetRequested(), which has the same shape.
    // The terminal deliberately takes that beat afterwards in onBetPlaced()
    // instead, which is what this class already does - so bidding is already
    // right and only the card path was missing one.
    if(!isHuman(seat))
        pace(Beat::Bot);
}

void GameBridge::onCardPlayed(const GameEngine& engine, Seat seat, const Card& card)
{
    // Still names this seat, for the reason onBetPlaced gives.
    publish(engine);
    emit cardPlayed(seat.index, card);

    // NOT PACED, and this is the second half of the beat onCardRequested takes.
    // Pacing both spends two beats per card and the second one dwells on a
    // table nothing has changed: the seats run back to back inside playTrick(),
    // so the frame after this one is the next seat's onCardRequested, which
    // already holds this card on screen for a beat while naming who is next.
    // The last card of a trick is held by onTrickWon's longer dwell instead.
    //
    // THE TERMINAL KEEPS THIS BEAT, and the divergence is deliberate rather than
    // overlooked. TerminalRomanianWhist paces in BOTH onCardRequested() and
    // onCardPlayed(), and says why: the second "gives the card a moment on
    // screen before another lands on top of it". That holds where the two
    // frames differ - the terminal's request frame adds "X is thinking..." -
    // and does not hold here, where the next frame names a different active
    // seat over an identical table.
    //
    // ASKED AGAIN AT B2, ON A TABLE, AND ANSWERED: it stays unpaced.
    //
    // A5 could only judge this off a text log, in which "the card landed" and
    // "the highlight moved to the next seat" are one line, and left the question
    // open for a phase that could draw both. B2 measured it. Without the beat
    // the middle state - card down, its player still ringed - lasts ONE
    // MILLISECOND, so the card landing and the ring moving are a single visual
    // event. With the beat it becomes a real 250ms state and every card costs
    // 500ms instead of 250, which is half the speed of a 26-round spectator
    // game.
    //
    // The reason it does not need the beat is the thing a table has and a log
    // does not: A CARD LANDS AT ITS OWNER'S SEAT AND STAYS THERE for the whole
    // trick. Who played it is carried by position - permanently, and by
    // TrickItem reading PlayedCardView::seatIndex - rather than by whoever the
    // ring happens to be on at the instant it appears. The worry this comment
    // recorded was "a card appearing under a highlight that has already left",
    // and on the drawn table it cannot: the ring is on a plaque out at the edge
    // and the card is in the middle, so it never appears under the ring at all.
    //
    // The terminal keeps its beat for the same reason it always did, and that
    // reason does not port: its only cue for who played what is a line of text
    // that scrolls away.
}

void GameBridge::onTrickWon(const GameEngine& engine, Seat seat, unsigned int trickNumber)
{
    // The finished trick is still the engine's CURRENT one throughout this
    // callback, so the snapshot carries the completed table rather than a blank
    // one. Note refreshFromEngine() reads the seats off that trick rather than
    // recomputing them from the leader, which has already moved to the winner.
    //
    // The trick is over and the next seat has not been asked, so nobody is on
    // turn for the dwell below - which is the longest a frame stays up.
    publish(engine);
    emit trickWon(seat.index, trickNumber);
    pace(Beat::TrickDwell);
}

RoundResult GameBridge::buildRoundResult(const GameEngine& engine) const
{
    RoundResult result;

    const unsigned int index = engine.getCurrentRoundIndex();

    result.roundNumber = index + 1;          // getCurrentRoundIndex() is 0-based
    result.roundCount = engine.getRoundCount();
    result.trickCount = engine.getCurrentRoundTrickCount();
    result.roundType = engine.getCurrentRoundType();

    const std::optional<Card> trump = engine.getCurrentTrumpCard();
    result.hasTrump = trump.has_value();
    if(trump)
        result.trump = *trump;

    // THE FIELD THIS TYPE EXISTS FOR. One-card rounds count for neither
    // direction of a streak, so a warning that the next miss costs 10 is wrong
    // when the next round deals one card. The schedule was laid out at start(),
    // so the round after this one already exists and can be asked - from HERE,
    // on the game thread, and nowhere else.
    //
    // The final round has no next round, so nothing can be warned about.
    if(index + 1 < result.roundCount)
        result.nextRoundCountsForStreaks = engine.getRound(index + 1).getTrickCount() != 1;

    const auto& players = engine.getPlayers();
    const unsigned int playerCount = engine.getPlayerCount();

    result.seats.resize(playerCount);

    for(unsigned int i = 0 ; i < playerCount ; i++)
    {
        const romanian_whist::Player& player = players.at(i);
        RoundSeatResult& seat = result.seats[i];
        const Seat at{ i };

        seat.name = player.getName();
        seat.seatIndex = i;
        seat.isHuman = isHuman(at);

        const std::optional<unsigned int> bet = engine.getBet(at);
        seat.hasBet = bet.has_value();
        if(bet)
            seat.bet = *bet;

        seat.tricksWon = engine.getTricksWon(at);

        // NOT SUMMED HERE, and the sheet is what adds them. Inside this callback
        // the round score is what the round was worth and the total is the one
        // it has not been folded into yet - which is exactly the pair section
        // 16.1 draws as "34 -> 41".
        seat.roundScore = engine.getRoundScore(at);
        seat.totalScore = engine.getTotalScore(at);

        seat.winStreak = player.getConsecutiveWins();
        seat.lossStreak = player.getConsecutiveLosses();
    }

    return result;
}

void GameBridge::onRoundScored(const GameEngine& engine)
{
    // This callback and not onRoundComplete: it is the last one at which the
    // engine still names the round just scored, so a round sheet drawn from it
    // shows the right round's numbers.
    publish(engine);

    // THE WAIT, READ ONCE, and both the result and the hold below go by that one
    // reading: Settings can change it at any moment, and the window learns
    // whether its sheet is holding from the result rather than from a copy of
    // its own (PR #35's review, finding #6).
    const bool held = waitsAfterEachRound();

    // BUILT BEFORE THE PACE, on the game thread, out of an engine that is about
    // to move on. By onRoundComplete() the index has advanced and every number
    // above would be attributed to the next round.
    RoundResult result = buildRoundResult(engine);
    result.held = held;

    emit roundScored(result);

    // HELD BEFORE THE WAIT, ON THIS THREAD, and not by the window that shows the
    // sheet. The emit above is queued: a window that set the hold on receiving
    // it would be racing the two lines below, and at the top of the speed slider
    // - where the dwell is zero - it loses every time.
    //
    // The window RELEASES it, on Continue. A release that arrives late is
    // harmless; a hold that arrives late does not hold anything.
    if(held)
        setPaused(true);

    // AND THE DWELL BELONGS HERE, on the frame that carries the result, not on
    // the blank one after it. Measured in phase A's log with the two the other
    // way round: onRoundScored published the result and returned, so that frame
    // stood for 0ms, and the 1.2s landed on onRoundComplete's advanced-index
    // frame - a next round with no trump, no bets and nothing played.
    //
    // THIS IS THE UNWAITED PATH'S DWELL. On the waited path the hold above IS the
    // beat, and pace() takes no timed wait at all - see the guard at the top of
    // it, and D2's review for what stacking the two cost.
    //
    // TerminalRomanianWhist does it this way too: it draws in onRoundScored()
    // and pauses there, and does not override onRoundComplete() at all. Its
    // roundPause() waits out the same option trickPause() uses, so matching it
    // needs no new knob here either.
    pace(Beat::TrickDwell);
}

void GameBridge::onRoundComplete(const GameEngine& engine)
{
    // Published but NOT paced - see onRoundScored above for where the dwell
    // went. This frame is the next round before it is dealt, which is nothing
    // anyone needs to read.
    publish(engine);
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

std::chrono::milliseconds GameBridge::delayOf(Beat beat) const
{
    if(beat == Beat::TrickDwell)
        return pacing.trickDwell;

    // humanSeat is set once, by the constructor, so it needs no lock of its own.
    return humanSeat ? pacing.beat : pacing.spectatorBeat;
}

bool GameBridge::waitsAfterEachRound()
{
    std::lock_guard lock(pacingMutex);
    return pacing.waitAfterEachRound;
}

void GameBridge::setPacing(Pacing _pacing)
{
    {
        std::lock_guard lock(pacingMutex);
        pacing = _pacing;
    }

    // Wakes a beat under way, which works its deadline out again (see pace()).
    pacingCv.notify_all();
}

void GameBridge::pace(Beat beat)
{
    std::unique_lock lock(pacingMutex);

    // NO TIMED DWELL ON A FRAME THAT IS ALREADY HELD. A dwell and a hold are the
    // same beat - both exist so the player can read the frame - and the hold is
    // the longer one, because it ends when the player says so. Stacking them
    // means a release has to wait the dwell out before anything moves: D2 shipped
    // that in onRoundScored(), where the frozen half was up to 2.4s of a Continue
    // click doing nothing at the bottom of the speed slider.
    //
    // The frame is published BEFORE pace() at every call site, so it is on screen
    // for the whole hold either way and nothing is skipped by skipping this.
    //
    // Read under the lock this already holds, so it costs no new synchronisation.
    //
    // THE DEADLINE IS WORKED OUT AGAIN ON EVERY WAKE, from the beat's start and
    // whatever its length is NOW: setPacing() notifies, so a speed changed in
    // Settings lands on the beat already under way. A wake for anything else -
    // a pause, a spurious one - finds the same deadline and waits on, so a pause
    // landing mid-delay still waits it out, as it always has.
    if(!pacingPaused)
    {
        const auto started = std::chrono::steady_clock::now();

        while(!pacingCancelled)
        {
            const auto deadline = started + delayOf(beat);

            if(std::chrono::steady_clock::now() >= deadline)
                break;

            pacingCv.wait_until(lock, deadline);
        }
    }

    // RETURNS on cancel. Does NOT throw - see the header. Getting this backwards
    // makes the clean shutdown path unreachable.

    // AND THEN HOLDS, indefinitely, while the game is paused. Written as a
    // second wait rather than folded into the predicate above so that the beat
    // is still a beat: a pause landing MID-DELAY waits the delay out first and
    // then holds, which costs at most one bot beat of latency and keeps the
    // timed half exactly what it was. Only a pause that was already set when we
    // arrived skips it, which is the guard above.
    //
    // pacingCancelled is in BOTH predicates, and that is the escape. Only
    // cancelPacing() can end this wait, and GameController::stop() calls it
    // unconditionally - so a close landing on a paused game returns here, back
    // into run(), and out through onGameStopped() like any other.
    pacingCv.wait(lock, [this]{ return pacingCancelled || !pacingPaused; });
}

void GameBridge::cancelPacing()
{
    {
        std::lock_guard lock(pacingMutex);
        pacingCancelled = true;
    }

    pacingCv.notify_all();
}

void GameBridge::setPaused(bool paused)
{
    {
        std::lock_guard lock(pacingMutex);
        pacingPaused = paused;
    }

    // notify_all rather than notify_one, matching cancelPacing() above: there is
    // one waiter today - the game thread - and picking the cheaper call would be
    // an assumption about that rather than a fact about this class.
    //
    // NOTIFYING ON A PAUSE, not only on a resume, is deliberate. Nothing is
    // waiting on the paused predicate at that moment, so the wake is wasted -
    // but writing it symmetrically is what stops a later reader from concluding
    // that one direction of this flag is somehow special.
    pacingCv.notify_all();
}
