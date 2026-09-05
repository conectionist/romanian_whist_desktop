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

TEST_CASE("The columns hold still when a field's numbers grow a digit", "[log]")
{
    // THE POINT OF THE PADDING, and the reason it is asserted rather than
    // eyeballed. Everything else in this file checks with contains(), which
    // cannot tell a padded field from an unpadded one - so R1/26 growing into
    // R10/26 and shoving every column after it one place to the right passed
    // the whole suite. In a log whose entire use is reading a value down the
    // page against the lines above it, that is the defect, not a detail.
    GameSnapshot narrow = twoSeats();
    narrow.betsPlaced = 2;
    narrow.betSum = 4;

    GameSnapshot wide = narrow;

    // One more digit in each of the two fields that grow one during a game: the
    // round number after round nine, and the bet sum once the seats bid past
    // nine tricks between them.
    wide.roundNumber = 10;
    wide.betSum = 13;

    const QString narrowLine = snapshotlog::format(narrow, kNoTime);
    const QString wideLine = snapshotlog::format(wide, kNoTime);

    // Both really are wider by a digit, or this test would pass on two
    // identical lines.
    REQUIRE(narrowLine.contains("R1/26"));
    REQUIRE(wideLine.contains("R10/26"));
    REQUIRE(narrowLine.contains("sum 4"));
    REQUIRE(wideLine.contains("sum 13"));

    // And every field after them starts at the same offset regardless.
    REQUIRE(narrowLine.indexOf("Normal") == wideLine.indexOf("Normal"));
    REQUIRE(narrowLine.indexOf("trump=") == wideLine.indexOf("trump="));
    REQUIRE(narrowLine.indexOf("active=") == wideLine.indexOf("active="));
    REQUIRE(narrowLine.indexOf("bets ") == wideLine.indexOf("bets "));
    REQUIRE(narrowLine.length() == wideLine.length());
}

TEST_CASE("A scored round does not render as the trick that ended it", "[log]")
{
    // THE ONE THIS FORMATTER EXISTS FOR. The frame published from onRoundScored
    // differs from the onTrickWon frame before it in the round score and in
    // nothing else, so a formatter that drops the score prints the same line
    // twice - and the 1.2s dwell on the second one reads as a single 2.4s hold
    // on unchanged text. What is asserted here is inequality, because two
    // identical lines is the defect itself.
    GameSnapshot snapshot = twoSeats();
    snapshot.trickNumber = 1;
    snapshot.seats[0].tricksWon = 1;

    // Mid-round: nothing scored and nothing committed, so no block at all - a
    // bidding frame is not padded with zeroes.
    const QString beforeScoring = snapshotlog::format(snapshot, kNoTime);
    REQUIRE_FALSE(beforeScoring.contains("scores["));

    // Scored, not yet committed - the engine writes getRoundScore() here and
    // folds it into getTotalScore() only on the commit that follows.
    snapshot.seats[0].roundScore = 7;
    snapshot.seats[1].roundScore = -1;

    const QString scored = snapshotlog::format(snapshot, kNoTime);

    REQUIRE(scored != beforeScoring);
    REQUIRE(scored.contains("scores[Ana:+7=0 Bogdan:-1=0]"));

    // Committed: the round score is cleared and the total carries it. Both
    // seats are still listed, so the columns line up down the log.
    snapshot.seats[0].roundScore = 0;
    snapshot.seats[1].roundScore = 0;
    snapshot.seats[0].totalScore = 7;
    snapshot.seats[1].totalScore = -1;

    const QString committed = snapshotlog::format(snapshot, kNoTime);

    REQUIRE(committed.contains("scores[Ana:7 Bogdan:-1]"));
    REQUIRE(committed != scored);
}

TEST_CASE("A seat that scored nothing is still listed, with a sign", "[log]")
{
    // +0 rather than dropped. A seat missing from the list is a seat a reader
    // has to go looking for, and a bare 0 reads as no data rather than as a
    // round that was worth nothing.
    GameSnapshot snapshot = twoSeats();
    snapshot.seats[0].roundScore = 5;
    snapshot.seats[1].roundScore = 0;
    snapshot.seats[1].totalScore = 3;

    REQUIRE(snapshotlog::format(snapshot, kNoTime).contains("scores[Ana:+5=0 Bogdan:+0=3]"));
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
