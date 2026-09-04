#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "core/Snapshot.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace romanian_whist;

// The anti-drift test QT_CLIENT_PLAN_proposal1.md section 9 asks for.
//
// refreshFromEngine() is a hand port of the terminal's GameView.cpp, kept in two
// repositories with nothing but discipline holding them together. This does not
// compare the two implementations - that would mean depending on the terminal -
// it pins THIS one, so that a change made to match a terminal fix has to be
// made deliberately and a change made by accident fails.

namespace
{
constexpr std::uint32_t kSeed = 42;

// Four seats, deterministic strategies only. RandomCardStrategy is deliberately
// excluded even seeded: its draw count depends on what every other seat bids, so
// any behavioural change desyncs its stream and turns a located failure into
// "something changed somewhere" - the engine's own golden suite says the same.
GameSetup buildSpectatorSetup()
{
    GameSetup setup;

    setup.seats.push_back(SeatSetup{ "Ana",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Bogdan",
        std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Cristina",
        std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Dan",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });

    setup.structure = GameStructure::S_181;
    setup.endWithForeheadAndHidden = true;
    setup.all1GamesAreForehead = false;
    setup.shuffleSeed = kSeed;

    return setup;
}

// Captures a snapshot at each of the four points section 8 names. `humanSeat`
// is what the bridge will set in publish(); it is set here for the same reason
// and, crucially, BEFORE the refresh.
struct Capture : IGameObserver
{
    int humanSeat = -1;

    std::optional<GameSnapshot> atStart;
    std::optional<GameSnapshot> firstDealt;
    std::optional<GameSnapshot> midBidding;
    std::optional<GameSnapshot> midTrick;
    std::optional<GameSnapshot> lastScored;

    // Every snapshot published in a Forehead or Hidden round, so the blind-hand
    // assertion can be made over all of them rather than one lucky frame.
    std::vector<GameSnapshot> blindRounds;

    GameSnapshot take(const GameEngine& engine)
    {
        GameSnapshot snapshot;
        snapshot.humanSeat = humanSeat;
        refreshFromEngine(snapshot, engine);

        if(snapshot.roundType != RoundType::Normal)
            blindRounds.push_back(snapshot);

        return snapshot;
    }

    void onGameStarted(const GameEngine& engine) override
    {
        atStart = take(engine);
    }

    // onGameStarted fires BEFORE the deal, so anything about cards has to be
    // asserted from here instead - dealt, trump known, no trick played yet.
    void onRoundStarted(const GameEngine& engine) override
    {
        const GameSnapshot snapshot = take(engine);

        if(!firstDealt)
            firstDealt = snapshot;
    }

    void onBetPlaced(const GameEngine& engine, Seat, unsigned int) override
    {
        // The first round in which two seats have bid and two have not, which
        // is what makes hasBet worth asserting.
        const GameSnapshot snapshot = take(engine);

        if(!midBidding && snapshot.betsPlaced == 2)
            midBidding = snapshot;
    }

    void onCardPlayed(const GameEngine& engine, Seat, const Card&) override
    {
        const GameSnapshot snapshot = take(engine);

        // A partly played trick: two cards down, two to come.
        if(!midTrick && snapshot.table.size() == 2)
            midTrick = snapshot;
    }

    void onRoundScored(const GameEngine& engine) override
    {
        // Overwritten each round, so what survives is the last one - the only
        // callback at which the engine still names the round just scored.
        lastScored = take(engine);
    }
};

std::unique_ptr<GameEngine> playFullGame(Capture& capture)
{
    auto engine = std::make_unique<GameEngine>();
    engine->addObserver(&capture);
    engine->start(buildSpectatorSetup());
    engine->run();
    return engine;
}
}

TEST_CASE("A snapshot taken at onGameStarted describes a dealt-but-unplayed round", "[snapshot]")
{
    Capture capture;
    const auto engine = playFullGame(capture);

    REQUIRE(capture.atStart.has_value());
    const GameSnapshot& snapshot = *capture.atStart;

    // roundNumber is 1-based; the engine's getCurrentRoundIndex() is not.
    REQUIRE(snapshot.roundNumber == 1);

    // 1-8-1 over four seats, plus the appended Forehead and Hidden rounds.
    REQUIRE(snapshot.roundCount == 26);
    REQUIRE(snapshot.trickCount == 1);
    REQUIRE(snapshot.roundType == RoundType::Normal);

    // NOT DEALT YET. onGameStarted fires as the last act of GameEngine::start(),
    // while the engine is still idle: the schedule exists and every round-scoped
    // accessor answers, but there is no trump and no card in any hand. So the
    // first snapshot the bridge ever publishes is a pre-deal frame, and anything
    // drawing cards from it draws nothing.
    REQUIRE_FALSE(snapshot.hasTrump);
    REQUIRE(snapshot.trickNumber == 0);
    REQUIRE(snapshot.table.empty());
    REQUIRE_FALSE(snapshot.hasLeadSuit);
    REQUIRE(snapshot.betsPlaced == 0);
    REQUIRE(snapshot.betSum == 0);

    REQUIRE(snapshot.seats.size() == 4);
    REQUIRE(snapshot.seats[0].name == "Ana");
    REQUIRE(snapshot.seats[3].name == "Dan");

    for(const SeatView& seat : snapshot.seats)
    {
        REQUIRE(seat.cardsLeft == 0);
        REQUIRE_FALSE(seat.hasBet);
        REQUIRE(seat.totalScore == 0);
        REQUIRE(seat.tricksWon == 0);
    }

    // A spectator game has no human seat, so no hand is filled for anyone.
    REQUIRE(snapshot.humanSeat == -1);
    REQUIRE(snapshot.hand.empty());
}

TEST_CASE("Bets in progress are told apart from a genuine bid of zero", "[snapshot]")
{
    Capture capture;
    const auto engine = playFullGame(capture);

    REQUIRE(capture.midBidding.has_value());
    const GameSnapshot& snapshot = *capture.midBidding;

    REQUIRE(snapshot.betsPlaced == 2);

    unsigned int seatsWithBets = 0;
    unsigned int summed = 0;

    for(const SeatView& seat : snapshot.seats)
    {
        if(seat.hasBet)
        {
            seatsWithBets++;
            summed += seat.bet;
        }
        else
        {
            // The field that makes hasBet necessary: a seat that has not bid
            // reads bet == 0, exactly as a seat that bid nothing does.
            REQUIRE(seat.bet == 0);
        }
    }

    REQUIRE(seatsWithBets == snapshot.betsPlaced);
    REQUIRE(summed == snapshot.betSum);

    // Bidding order is a permutation of 1..4, whoever opened the round.
    std::vector<unsigned int> orders;
    for(const SeatView& seat : snapshot.seats)
        orders.push_back(seat.bidOrder);

    std::sort(orders.begin(), orders.end());
    REQUIRE(orders == std::vector<unsigned int>{ 1, 2, 3, 4 });
}

TEST_CASE("A partly played trick reads its seats off the trick, not the leader", "[snapshot]")
{
    Capture capture;
    const auto engine = playFullGame(capture);

    REQUIRE(capture.midTrick.has_value());
    const GameSnapshot& snapshot = *capture.midTrick;

    REQUIRE(snapshot.table.size() == 2);
    REQUIRE(snapshot.hasLeadSuit);

    // leadSeat is whoever LED this trick, which is the seat of its first card.
    // getTrickLeaderSeat() is not that from the moment a trick is won.
    REQUIRE(snapshot.leadSeat == snapshot.table.front().seatIndex);
    REQUIRE(snapshot.table.front().card.suit == snapshot.leadSuit);

    // Exactly one card is winning, and it is the engine's answer rather than
    // anything recomputed here.
    unsigned int winning = 0;
    for(const PlayedCardView& played : snapshot.table)
    {
        if(played.isWinning)
            winning++;
    }

    REQUIRE(winning == 1);
}

TEST_CASE("The last scored round is the last round in the schedule", "[snapshot]")
{
    Capture capture;
    const auto engine = playFullGame(capture);

    REQUIRE(capture.lastScored.has_value());
    const GameSnapshot& snapshot = *capture.lastScored;

    // onRoundScored is the last callback at which the engine still names the
    // round it just scored, which is why the round sheet is drawn from it.
    REQUIRE(snapshot.roundNumber == snapshot.roundCount);
    REQUIRE(snapshot.roundNumber == 26);

    // endWithForeheadAndHidden appends a Forehead round and then a Hidden one.
    REQUIRE(snapshot.roundType == RoundType::Hidden);

    // Scores are calculated but not yet committed at this callback, so the two
    // are readable side by side - which is what the round sheet needs.
    for(const SeatView& seat : snapshot.seats)
        REQUIRE(seat.cardsLeft == 0);
}

TEST_CASE("The human's own hand is empty in a Forehead or Hidden round", "[snapshot]")
{
    // The assertion phase C stalls on if it is wrong: canSeeHand(human, human)
    // is false in both round types, so a snapshot-driven hand widget shows
    // nothing while the engine parks on a card it will never be given.
    Capture capture;
    capture.humanSeat = 0;

    const auto engine = playFullGame(capture);

    REQUIRE_FALSE(capture.blindRounds.empty());

    bool sawForehead = false;
    bool sawHidden = false;

    for(const GameSnapshot& snapshot : capture.blindRounds)
    {
        REQUIRE(snapshot.humanSeat == 0);
        REQUIRE(snapshot.hand.empty());

        sawForehead = sawForehead || snapshot.roundType == RoundType::Forehead;
        sawHidden = sawHidden || snapshot.roundType == RoundType::Hidden;

        // Forehead is the round where everyone else's hand IS visible, which is
        // the other half of the same canSeeHand() question.
        if(snapshot.roundType == RoundType::Forehead)
        {
            for(const SeatView& seat : snapshot.seats)
            {
                if(seat.seatIndex != 0 && seat.cardsLeft > 0)
                    REQUIRE(seat.hand.size() == seat.cardsLeft);
            }
        }

        // Hidden: nobody's hand is visible, including everyone else's.
        if(snapshot.roundType == RoundType::Hidden)
        {
            for(const SeatView& seat : snapshot.seats)
                REQUIRE(seat.hand.empty());
        }
    }

    REQUIRE(sawForehead);
    REQUIRE(sawHidden);
}

TEST_CASE("A seated human sees their own hand in a normal round", "[snapshot]")
{
    // The counterweight to the test above: if `hand` were empty everywhere, that
    // one would pass for the wrong reason.
    Capture capture;
    capture.humanSeat = 0;

    const auto engine = playFullGame(capture);

    // From onRoundStarted, NOT onGameStarted - the latter fires before the deal,
    // where every hand is empty and this would pass with 0 == 0.
    REQUIRE(capture.firstDealt.has_value());
    const GameSnapshot& snapshot = *capture.firstDealt;

    REQUIRE(snapshot.humanSeat == 0);
    REQUIRE(snapshot.roundType == RoundType::Normal);
    REQUIRE(snapshot.seats[0].cardsLeft > 0);
    REQUIRE(snapshot.hand.size() == snapshot.seats[0].cardsLeft);

    // A snapshot's hand carries no legality - it is laid out with an empty legal
    // set, which is what says "nothing is being chosen here".
    for(const HandEntry& entry : snapshot.hand)
        REQUIRE(entry.legal);

    // And in a Normal round nobody else's hand is visible.
    for(const SeatView& seat : snapshot.seats)
    {
        if(seat.seatIndex != 0)
            REQUIRE(seat.hand.empty());
    }
}
