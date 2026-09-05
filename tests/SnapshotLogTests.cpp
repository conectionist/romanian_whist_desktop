#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "ui/SnapshotLog.h"

#include <chrono>
#include <vector>

using namespace romanian_whist;

// The one part of phase A's window with a right answer, so the one part worth a
// test. What it pins is not prettiness: it is that a frame the engine can
// actually produce - before the deal, mid-trick, in a blind round - renders as
// what it is rather than as a default-constructed Card or an out-of-range seat.

namespace
{
constexpr auto kNoTime = std::chrono::milliseconds(0);

GameSnapshot twoSeats()
{
    GameSnapshot snapshot;
    snapshot.roundNumber = 1;
    snapshot.roundCount = 26;
    snapshot.trickCount = 1;
    snapshot.seats.resize(2);
    snapshot.seats[0].name = "Ana";
    snapshot.seats[0].seatIndex = 0;
    snapshot.seats[1].name = "Bogdan";
    snapshot.seats[1].seatIndex = 1;
    return snapshot;
}
}

TEST_CASE("A pre-deal snapshot shows no trump and nobody on turn", "[log]")
{
    // The FIRST frame every game publishes. onGameStarted() fires while the
    // engine is still idle, so there is no trump and no card in any hand - and
    // printing snapshot.trump regardless would show the two of clubs, which is
    // Card's default and a real card a reader would believe.
    const GameSnapshot snapshot = twoSeats();

    const QString line = snapshotlog::format(snapshot, kNoTime);

    REQUIRE(line.contains("trump=-"));
    REQUIRE(line.contains("active=-"));
    REQUIRE_FALSE(line.contains("2C"));

    // Nothing on the table, so the table section is absent rather than empty.
    REQUIRE_FALSE(line.contains("table="));

    // Round and trick as a reader expects them: 1-based, and trick 0 meaning
    // "dealt, nothing played" rather than an off-by-one.
    REQUIRE(line.contains("R1/26"));
    REQUIRE(line.contains("T0/1"));
}

TEST_CASE("A partly played trick lists the table in play order, starring the winner", "[log]")
{
    GameSnapshot snapshot = twoSeats();
    snapshot.trickNumber = 1;
    snapshot.hasTrump = true;
    snapshot.trump = Card(Rank::Jack, Suit::Hearts);
    snapshot.activeSeat = 1;
    snapshot.leadSeat = 1;

    // Bogdan led, Ana followed and is winning - so play order is NOT seat
    // order, which is the thing worth pinning: the table is read off the
    // engine's trick, never rebuilt from the leader.
    snapshot.table.push_back({ 1, Card(Rank::Three, Suit::Spades), false });
    snapshot.table.push_back({ 0, Card(Rank::Ace, Suit::Spades), true });

    const QString line = snapshotlog::format(snapshot, std::chrono::milliseconds(1250));

    REQUIRE(line.contains("trump=JH"));
    REQUIRE(line.contains("active=Bogdan"));
    REQUIRE(line.contains("lead=Bogdan"));
    REQUIRE(line.contains("table=[Bogdan 3S, Ana AS*]"));

    // Exactly one star - isWinning is the engine's ranking and only one card
    // carries it.
    REQUIRE(line.count('*') == 1);

    // 1250ms reads as seconds, which is the unit the pacing question is asked
    // in: 0.25 against 1.2, not 250 against 1200.
    REQUIRE(line.contains("1.250"));
}

TEST_CASE("A blind round is named without leaking the hand", "[log]")
{
    // Forehead and Hidden. The snapshot's own hand is already empty in both -
    // canSeeHand(human, human) is false - but a spectator game fills no hand
    // for anyone either, so what this pins is that the round TYPE is reported.
    // A log that called every round Normal would hide the two rounds phase C
    // stalls in.
    GameSnapshot snapshot = twoSeats();
    snapshot.roundNumber = 26;
    snapshot.roundType = RoundType::Hidden;

    const QString line = snapshotlog::format(snapshot, kNoTime);

    REQUIRE(line.contains("Hidden"));
    REQUIRE(line.contains("R26/26"));

    snapshot.roundType = RoundType::Forehead;
    REQUIRE(snapshotlog::format(snapshot, kNoTime).contains("Forehead"));
}

TEST_CASE("Bets and tricks won appear only once there are any", "[log]")
{
    GameSnapshot snapshot = twoSeats();

    // A bidding frame is not padded with zeroes for seats that have won
    // nothing - there is a lot of this log and the noise adds up.
    REQUIRE_FALSE(snapshotlog::format(snapshot, kNoTime).contains("won["));

    snapshot.seats[0].hasBet = true;
    snapshot.seats[0].bet = 1;
    snapshot.betsPlaced = 1;
    snapshot.betSum = 1;
    snapshot.seats[0].tricksWon = 1;

    const QString line = snapshotlog::format(snapshot, kNoTime);

    REQUIRE(line.contains("bets 1/2"));
    REQUIRE(line.contains("sum 1"));
    REQUIRE(line.contains("won[Ana:1]"));
}

TEST_CASE("A seat index the snapshot cannot name renders as nobody", "[log]")
{
    // -1 is the snapshot's way of saying "no seat", because Seat has no default
    // constructor and no sentinel of its own. Anything out of range is the same
    // answer rather than an out-of-bounds read - this formatter runs on every
    // frame and must not be the thing that crashes the window.
    GameSnapshot snapshot = twoSeats();
    snapshot.activeSeat = 99;

    REQUIRE(snapshotlog::format(snapshot, kNoTime).contains("active=-"));
}
