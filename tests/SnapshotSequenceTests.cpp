#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameFixtures.h"

#include "core/GameSnapshot.h"
#include "engine/GameBridge.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/Seat.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace romanian_whist;

// Phase A's acceptance test: what must be true of the SEQUENCE of snapshots a
// real game produces, rather than of any one of them.
//
// This replaces what the build order specifies - diffing the log against the
// terminal client's transcript for the same seed. The reasons are in
// docs/PHASE_A_PLAN.md: the two outputs are structurally incompatible (the
// terminal redraws the whole screen per event, 19,040 lines to this client's
// ~1,300), it couples two clients that are documented as meant to diverge, and
// the desktop repository has no access to the terminal at run time.
//
// WHAT THIS IS NOT is a re-test of the engine. The engine's own suite asserts
// that the game is played correctly, seeded and golden, and it is thorough. The
// assertions below are over the snapshots THIS CLIENT builds, so a projection
// that drops a card or names the wrong seat fails here while the engine stays
// perfectly correct - which is the only bug class phase A can own.
//
// EVERY INVARIANT BELOW HAS A NAMED MUTATION that must make it fail, listed in
// the comment above it and verified by applying it. That discipline is not
// decoration: this phase has already produced four assertions that passed for
// the wrong reason - a hand comparison that was 0 == 0, a shutdown test that
// passed on a timeout, pacing tests that did not pin the pacing, and an ASan
// recipe that ran an uninstrumented binary. A green suite proves nothing about
// a test nobody has watched fail.

namespace
{
constexpr std::size_t kSeats = 4;

// Every snapshot the bridge publishes over a whole game, in order.
//
// COLLECTED THROUGH THE BRIDGE rather than by calling refreshFromEngine
// directly, which is what puts publish()'s own work - humanSeat - under test
// alongside the projection. It is also the stream the window actually sees.
// (activeSeat is publish()'s other job and is NOT asserted here; the whole-game
// agreement between it and the engine is GameBridgeTests' "Every snapshot names
// the seat on turn, or nobody".)
//
// No thread anywhere: the engine runs on this thread, and the bridge is an
// observer like any other. A6 adds no concurrency.
std::vector<GameSnapshot> playAndCollect(std::optional<Seat> humanSeat = std::nullopt)
{
    GameBridge bridge(humanSeat, fixtures::instant());

    std::vector<GameSnapshot> frames;

    QObject::connect(&bridge, &GameBridge::stateChanged,
                     [&frames](std::shared_ptr<const GameSnapshot> snapshot)
                     { frames.push_back(*snapshot); });

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.start(fixtures::buildSetup(kSeats));
    engine.run();

    // THE FLOOR EVERY CASE STANDS ON, asserted here rather than trusted ten
    // times over. The cases below index frames.front(), scored.back() and
    // seats[seat] directly, so a stream that came back empty or short has to
    // fail with a message rather than read off the end - including when one
    // case is run on its own or under --order rand, where no other case has
    // gone first to notice.
    REQUIRE(frames.size() > 500);
    REQUIRE(std::all_of(frames.begin(), frames.end(),
                        [](const GameSnapshot& frame)
                        { return frame.seats.size() == kSeats; }));

    return frames;
}

// THE LAST FRAME CARRYING EACH ROUND NUMBER. For rounds 1..n-1 that is the
// onRoundScored frame, because onRoundComplete publishes the already-advanced
// index and so belongs to the next round.
//
// THE FINAL ROUND IS DIFFERENT, and finding that out cost a failing run:
// completeCurrentRound() leaves the index where it is on the last round, so
// onRoundComplete AND onGameOver both publish round n - after the scores are
// committed. The last frame of round n therefore reports roundScore 0 and a
// total that already includes it, where every other round's reports the
// opposite. Use scoredFrameOfEachRound() for anything about scores.
std::vector<GameSnapshot> lastFrameOfEachRound(const std::vector<GameSnapshot>& frames)
{
    std::vector<GameSnapshot> ends;

    for(std::size_t i = 0 ; i < frames.size() ; i++)
    {
        const bool last = (i + 1 == frames.size()) ||
                          frames[i + 1].roundNumber != frames[i].roundNumber;

        if(last)
            ends.push_back(frames[i]);
    }

    return ends;
}

// THE onRoundScored FRAME OF EACH ROUND, identified by the one thing that is
// true there and nowhere else: the round's score is engaged.
//
// Scoreboard::calculateRoundScore returns 5 + bid for a made bid and a negative
// number for a missed one, so it is NEVER zero once scored - and it is zero
// everywhere else in the round, because commitRoundScores() resets it. That
// makes "roundScore != 0" an exact identification rather than a heuristic, and
// one that works on the final round too, where the last frame is committed.
std::vector<GameSnapshot> scoredFrameOfEachRound(const std::vector<GameSnapshot>& frames)
{
    std::map<unsigned int, GameSnapshot> scored;

    for(const GameSnapshot& frame : frames)
    {
        if(frame.seats.front().roundScore != 0)
            scored.insert_or_assign(frame.roundNumber, frame);
    }

    std::vector<GameSnapshot> ordered;
    for(auto& [round, frame] : scored)
        ordered.push_back(frame);

    return ordered;
}

bool tableIsComplete(const GameSnapshot& frame)
{
    return frame.table.size() == frame.seats.size();
}
}

TEST_CASE("A game publishes a stream at all", "[sequence]")
{
    // The floor every case below stands on. If the stream were empty or a
    // handful of frames, every "for every frame ..." assertion in this file
    // would pass by iterating nothing - which is exactly the failure mode this
    // suite exists to avoid.
    //
    // playAndCollect() now enforces the same floor for every case, so no case
    // depends on this one having run first. It is restated here because this is
    // where a reader looks for it, and because the full-schedule check below
    // belongs nowhere else.
    const std::vector<GameSnapshot> frames = playAndCollect();

    REQUIRE(frames.size() > 500);

    for(const GameSnapshot& frame : frames)
        REQUIRE(frame.seats.size() == kSeats);

    // And it really did play a whole schedule rather than stopping early.
    REQUIRE(frames.back().roundNumber == frames.back().roundCount);
}

TEST_CASE("The lead seat never drifts to the winner", "[sequence]")
{
    // MUTATION: rebuildTable() using game.getTrickLeaderSeat().index
    // unconditionally instead of played.front().seat.index.
    //
    // Asserting leadSeat == table.front().seatIndex would be worthless - that
    // IS the assignment in Snapshot.cpp. The real property is CONSTANCY: the
    // engine moves getTrickLeaderSeat() to the winner the moment a trick is
    // decided, and onTrickWon still carries the completed table, so a leadSeat
    // read from the engine is wrong on exactly one frame per trick. Only
    // comparing frames catches that.
    const std::vector<GameSnapshot> frames = playAndCollect();

    unsigned int tricksSeen = 0;
    unsigned int completedFramesSeen = 0;

    std::optional<unsigned int> leadThisTrick;
    unsigned int currentRound = 0;
    unsigned int currentTrick = 0;

    for(const GameSnapshot& frame : frames)
    {
        if(frame.roundNumber != currentRound || frame.trickNumber != currentTrick)
        {
            currentRound = frame.roundNumber;
            currentTrick = frame.trickNumber;
            leadThisTrick.reset();
        }

        // Only frames that actually carry a trick. With an empty table leadSeat
        // falls back to getTrickLeaderSeat(), which after a win IS the winner -
        // correctly, since there is no trick to lead yet.
        if(frame.table.empty())
            continue;

        if(!leadThisTrick)
        {
            leadThisTrick = frame.leadSeat;
            tricksSeen++;
        }

        REQUIRE(frame.leadSeat == *leadThisTrick);

        if(tableIsComplete(frame))
            completedFramesSeen++;
    }

    // A 4-seat S_181 schedule with the two appended rounds plays 96 tricks.
    REQUIRE(tricksSeen == 96);

    // AND THE FRAMES WHERE THE DRIFT WOULD SHOW WERE ACTUALLY REACHED. The
    // count is spelled out rather than loosened to a lower bound, because it
    // documents the shape of the stream and a change to that shape is worth
    // being told about:
    //
    //   2 per trick   - the last onCardPlayed, then onTrickWon
    // + 1 per round   - onRoundScored, because the trick is NOT cleared until
    //                   the next round's deal, so the completed table is still
    //                   standing there
    // + 2             - the final round's onRoundComplete and onGameOver, which
    //                   publish the same round number because the index does
    //                   not advance on the last round
    REQUIRE(completedFramesSeen == 96 * 2 + 26 + 2);
}

TEST_CASE("Exactly one card on the table is winning", "[sequence]")
{
    // MUTATION: in rebuildTable(), set isWinning on none of the entries, or on
    // all of them.
    //
    // isWinning is the engine's own ranking, asked once per card so a partly
    // played trick is answered too. A table with none marked, or two, means the
    // highlight can disagree with the trick the engine eventually awards.
    const std::vector<GameSnapshot> frames = playAndCollect();

    unsigned int tablesChecked = 0;

    for(const GameSnapshot& frame : frames)
    {
        if(frame.table.empty())
            continue;

        const auto winners = std::count_if(frame.table.begin(), frame.table.end(),
                                           [](const PlayedCardView& c){ return c.isWinning; });

        REQUIRE(winners == 1);
        tablesChecked++;
    }

    REQUIRE(tablesChecked > 300);
}

TEST_CASE("A completed trick holds one card per seat, each seat once", "[sequence]")
{
    // MUTATION: push a constant seat index in rebuildTable() rather than
    // entry.seat.index.
    //
    // The seats are read off the engine's trick rather than recomputed as
    // (leader + i) % count - the engine's own header warns against the latter -
    // so this pins that the reading is faithful.
    const std::vector<GameSnapshot> frames = playAndCollect();

    unsigned int completed = 0;

    for(const GameSnapshot& frame : frames)
    {
        if(!tableIsComplete(frame))
            continue;

        std::set<unsigned int> seatsOnTable;
        for(const PlayedCardView& played : frame.table)
            seatsOnTable.insert(played.seatIndex);

        // Every seat exactly once: a set of the same size as the table.
        REQUIRE(seatsOnTable.size() == frame.table.size());

        for(unsigned int seat = 0 ; seat < kSeats ; seat++)
            REQUIRE(seatsOnTable.count(seat) == 1);

        completed++;
    }

    // Same arithmetic as the lead-seat case above, which explains it.
    REQUIRE(completed == 96 * 2 + 26 + 2);
}

TEST_CASE("Tricks won over a round sum to the round's trick count", "[sequence]")
{
    // MUTATION: seat.tricksWon = 0 in refreshFromEngine().
    const std::vector<GameSnapshot> frames = playAndCollect();
    const std::vector<GameSnapshot> ends = lastFrameOfEachRound(frames);

    REQUIRE(ends.size() == frames.front().roundCount);

    for(const GameSnapshot& end : ends)
    {
        unsigned int won = 0;
        for(const SeatView& seat : end.seats)
            won += seat.tricksWon;

        REQUIRE(won == end.trickCount);
    }
}

TEST_CASE("Totals accumulate across the round boundary", "[sequence]")
{
    // MUTATION: seat.totalScore = game.getRoundScore(...) in
    // refreshFromEngine().
    //
    // AT onRoundScored THE ROUND IS CALCULATED BUT NOT COMMITTED, so totalScore
    // is still the pre-round total and roundScore is what the round was worth.
    // Player::resetCurrentRoundScore() is what folds one into the other, and
    // the engine calls it AFTER this callback. So each scored frame's total is
    // the previous scored frame's total plus that frame's round score.
    //
    // This is asserted rather than trusted. The plan flagged that if the commit
    // order were the other way round the invariant would be off by one and
    // "fixed" in the wrong direction - and the first run did fail here, though
    // for a different reason: the LAST frame of the final round is committed,
    // because the index does not advance there. scoredFrameOfEachRound() is
    // what avoids that trap.
    const std::vector<GameSnapshot> frames = playAndCollect();
    const std::vector<GameSnapshot> scored = scoredFrameOfEachRound(frames);

    REQUIRE(scored.size() == frames.front().roundCount);

    unsigned int comparisons = 0;

    for(std::size_t i = 1 ; i < scored.size() ; i++)
    {
        const GameSnapshot& previous = scored[i - 1];
        const GameSnapshot& current = scored[i];

        for(std::size_t seat = 0 ; seat < kSeats ; seat++)
        {
            REQUIRE(current.seats[seat].totalScore ==
                    previous.seats[seat].totalScore + previous.seats[seat].roundScore);
            comparisons++;
        }
    }

    // Not a vacuous walk: 25 boundaries across four seats.
    REQUIRE(comparisons == (frames.front().roundCount - 1) * kSeats);

    // And the scores actually moved, or every addition above is zero plus zero
    // and would survive a mutation that also returned zero.
    REQUIRE(scored.back().seats[0].totalScore != 0);
}

TEST_CASE("The final round is committed by the time the game is over", "[sequence]")
{
    // The other half of the invariant above, and the reason its first version
    // failed. On the last round completeCurrentRound() leaves the index where
    // it is, so onRoundComplete and onGameOver both publish that round number -
    // after commitRoundScores() has folded the round into the total and reset
    // it. Every other round's last frame reports the opposite.
    //
    // Worth pinning rather than merely working around: a window drawing final
    // standings reads this frame, and it is the only one in the game where
    // roundScore is zero AFTER a round was scored rather than before.
    const std::vector<GameSnapshot> frames = playAndCollect();
    const std::vector<GameSnapshot> scored = scoredFrameOfEachRound(frames);
    const GameSnapshot& last = frames.back();

    REQUIRE(last.roundNumber == last.roundCount);

    for(std::size_t seat = 0 ; seat < kSeats ; seat++)
    {
        // Committed: nothing left uncounted.
        REQUIRE(last.seats[seat].roundScore == 0);

        // And the final round's points are IN the total, which is what
        // distinguishes "committed" from "never scored".
        REQUIRE(last.seats[seat].totalScore ==
                scored.back().seats[seat].totalScore + scored.back().seats[seat].roundScore);
    }
}

TEST_CASE("Cards leave a hand exactly as they reach the table", "[sequence]")
{
    // MUTATION: seat.cardsLeft = frame.trickCount (a constant) in
    // refreshFromEngine().
    //
    // Per round: every seat is dealt trickCount cards, loses exactly one each
    // time it appears on the table, and finishes empty. This is the closest a
    // SPECTATOR game can get to card conservation - no hand is filled for
    // anyone without a seated viewer - and invariant 9 below does the rest.
    const std::vector<GameSnapshot> frames = playAndCollect();

    unsigned int roundsChecked = 0;
    unsigned int currentRound = 0;

    std::vector<unsigned int> playedBySeat(kSeats, 0);
    std::optional<unsigned int> dealt;
    std::set<std::pair<unsigned int, unsigned int>> countedThisRound;   // (trick, seat)

    const auto closeRound = [&](const GameSnapshot& last)
    {
        if(!dealt)
            return;

        for(std::size_t seat = 0 ; seat < kSeats ; seat++)
        {
            REQUIRE(last.seats[seat].cardsLeft == 0);
            REQUIRE(playedBySeat[seat] == *dealt);
        }

        roundsChecked++;
    };

    const GameSnapshot* previous = nullptr;

    for(const GameSnapshot& frame : frames)
    {
        if(frame.roundNumber != currentRound)
        {
            if(previous)
                closeRound(*previous);

            currentRound = frame.roundNumber;
            dealt.reset();
            countedThisRound.clear();
            std::fill(playedBySeat.begin(), playedBySeat.end(), 0);
        }

        // The deal: the first frame of the round on which anyone holds a card.
        if(!dealt && frame.seats[0].cardsLeft > 0)
        {
            dealt = frame.trickCount;

            for(const SeatView& seat : frame.seats)
                REQUIRE(seat.cardsLeft == frame.trickCount);
        }

        // Count each (trick, seat) card once, however many frames carry it.
        for(const PlayedCardView& played : frame.table)
        {
            if(countedThisRound.insert({ frame.trickNumber, played.seatIndex }).second)
                playedBySeat[played.seatIndex]++;
        }

        if(dealt)
        {
            for(std::size_t seat = 0 ; seat < kSeats ; seat++)
                REQUIRE(frame.seats[seat].cardsLeft + playedBySeat[seat] == *dealt);
        }

        previous = &frame;
    }

    if(previous)
        closeRound(*previous);

    REQUIRE(roundsChecked == frames.front().roundCount);
}

TEST_CASE("The schedule runs 1..roundCount and ends Forehead then Hidden", "[sequence]")
{
    // MUTATION: drop the + 1 in roundNumber = getCurrentRoundIndex() + 1.
    const std::vector<GameSnapshot> frames = playAndCollect();
    const std::vector<GameSnapshot> ends = lastFrameOfEachRound(frames);

    const unsigned int count = frames.front().roundCount;
    REQUIRE(count == 26);
    REQUIRE(ends.size() == count);

    for(std::size_t i = 0 ; i < ends.size() ; i++)
        REQUIRE(ends[i].roundNumber == static_cast<unsigned int>(i) + 1);

    // endWithForeheadAndHidden appends one of each, in that order, and those
    // two rounds are where phase C stalls if the hand is driven by snapshots.
    REQUIRE(ends[count - 2].roundType == RoundType::Forehead);
    REQUIRE(ends[count - 1].roundType == RoundType::Hidden);

    // Every other round is Normal, so the two above are not just the tail of a
    // schedule that is Forehead throughout.
    for(std::size_t i = 0 ; i + 2 < ends.size() ; i++)
        REQUIRE(ends[i].roundType == RoundType::Normal);
}

TEST_CASE("The bids never sum to the trick count", "[sequence]")
{
    // MUTATION: seat.bet = game.getTricksWon(...) in refreshFromEngine().
    //
    // The engine's forbidden-bet rule, cross-checked through the projection
    // rather than restated from it. Asserting betSum against the per-seat bets
    // would be worthless - that is the same loop in Snapshot.cpp - but the rule
    // itself is the engine's, so a projection reading bets from the wrong place
    // breaks it.
    //
    // THE NAMED MUTATION MATTERS MORE HERE THAN ANYWHERE ELSE IN THIS FILE,
    // because this is a one-sided assertion and most wrong answers still
    // satisfy it. Reporting bidOrder as the bet does NOT fail this - those sum
    // to 10 across four seats and a trick count never exceeds 8 - which was
    // tried first and passed. Tricks won is the mutation that lands: once a
    // round is fully played they sum to exactly the trick count, so the
    // forbidden total is hit and the rule reports it.
    //
    // That is the difference between an invariant that holds and one that
    // guards. This one guards narrowly; it is kept because the rule is real,
    // not because it covers much.
    const std::vector<GameSnapshot> frames = playAndCollect();

    unsigned int fullyBidFrames = 0;

    for(const GameSnapshot& frame : frames)
    {
        if(frame.betsPlaced != frame.seats.size())
            continue;

        REQUIRE(frame.betSum != frame.trickCount);
        fullyBidFrames++;
    }

    REQUIRE(fullyBidFrames > 100);
}

TEST_CASE("A seated player's hand and their played cards partition the deal", "[sequence]")
{
    // MUTATION: in refreshFromEngine(), lay out a different seat's hand than
    // the human's.
    //
    // THE REASON THE SEATED PASS EXISTS. A spectator game fills no hand for
    // anyone, so every hand assertion above it would be vacuous. Here `hand`
    // comes from layOutHand(player.getHand()) and the played cards come off the
    // table stream - different code, different engine state - so the two
    // agreeing is a real cross-check rather than a restatement.
    //
    // Seat 0 is still driven by an AiMoveProvider; humanSeat only decides whose
    // hand the snapshot is built for.
    const std::vector<GameSnapshot> frames = playAndCollect(Seat{0});

    unsigned int roundsChecked = 0;
    unsigned int currentRound = 0;
    std::set<std::string> playedBySeatZero;
    std::set<std::pair<unsigned int, unsigned int>> counted;
    std::optional<std::set<std::string>> dealtToSeatZero;

    // ON THE ROUND'S LAST FRAME, not somewhere inside it: by the time the round
    // ends the hand is empty and every card dealt to seat 0 has been played.
    //
    // Counting per ROUND is the point. An increment inside the loop below fires
    // on every frame after seat 0's last card - the final onCardPlayed, the
    // onTrickWon, the onRoundScored - which turns the tally into a frame count
    // that no per-round expectation can be written against.
    const auto closeRound = [&](const GameSnapshot& last)
    {
        // A blind round, or one whose hand never came into view: nothing was
        // dealt to partition, and there is nothing to close.
        if(!dealtToSeatZero)
            return;

        REQUIRE(last.hand.empty());
        REQUIRE(playedBySeatZero.size() == dealtToSeatZero->size());

        roundsChecked++;
    };

    const GameSnapshot* previous = nullptr;

    for(const GameSnapshot& frame : frames)
    {
        if(frame.roundNumber != currentRound)
        {
            if(previous)
                closeRound(*previous);

            currentRound = frame.roundNumber;
            playedBySeatZero.clear();
            counted.clear();
            dealtToSeatZero.reset();
        }

        // Recorded here rather than at the end of the body, because the blind
        // round below continues past it.
        previous = &frame;

        REQUIRE(frame.humanSeat == 0);

        // Blind rounds: canSeeHand(human, human) is false in both, so there is
        // nothing to partition and nothing to assert beyond the emptiness that
        // A1 already pins.
        if(frame.roundType != RoundType::Normal)
        {
            REQUIRE(frame.hand.empty());
            continue;
        }

        // The dealt hand, taken once, on the first frame that carries one.
        if(!dealtToSeatZero && !frame.hand.empty())
        {
            std::set<std::string> dealt;
            for(const HandEntry& entry : frame.hand)
                dealt.insert(entry.card.toString());

            REQUIRE(dealt.size() == frame.trickCount);
            dealtToSeatZero = dealt;
        }

        for(const PlayedCardView& played : frame.table)
        {
            if(played.seatIndex == 0 &&
               counted.insert({ frame.trickNumber, played.seatIndex }).second)
                playedBySeatZero.insert(played.card.toString());
        }

        if(!dealtToSeatZero)
            continue;

        std::set<std::string> inHand;
        for(const HandEntry& entry : frame.hand)
            inHand.insert(entry.card.toString());

        // NO CARD IN BOTH: a card on the table is gone from the hand.
        for(const std::string& card : playedBySeatZero)
            REQUIRE(inHand.count(card) == 0);

        // AND NONE MISSING: hand + played is exactly what was dealt.
        std::set<std::string> union_ = inHand;
        union_.insert(playedBySeatZero.begin(), playedBySeatZero.end());

        REQUIRE(union_ == *dealtToSeatZero);
    }

    if(previous)
        closeRound(*previous);

    // EVERY Normal round reached that point, not merely one of them: 26 rounds
    // less the two appended blind ones, which carry no visible hand. Spelled
    // out rather than left as "> 0" for the reason the trick counts above are -
    // a lower bound here would pass on a projection that broke 23 of the 24.
    REQUIRE(roundsChecked == 24);
}

