#include <catch2/catch_test_macros.hpp>

#include "ui/ScoreboardWidget.h"

#include <QString>

#include <vector>

// What the scoreboard SAYS, tested without a window.
//
// The split is SnapshotLog's, and its header states the argument: the formatter
// is the only part of the panel with a right answer, so it is the only part
// worth a test. Whether the columns line up is a thing to look at; whether a
// seat that has not bid reads as having bid nothing is not.
namespace
{

SeatView seatWith(const char* name, unsigned int bidOrder)
{
    SeatView seat;

    seat.name = name;
    seat.bidOrder = bidOrder;

    return seat;
}

GameSnapshot fourSeats()
{
    GameSnapshot snapshot;

    snapshot.seats.push_back(seatWith("Ana", 1));
    snapshot.seats.push_back(seatWith("Bogdan", 2));
    snapshot.seats.push_back(seatWith("Cristina", 3));
    snapshot.seats.push_back(seatWith("Dan", 4));

    return snapshot;
}

}

// hasBet is on the snapshot for exactly this, and its own comment says so: it
// "tells a genuine bid of 0 from no bid at all". A scoreboard that reads `bet`
// without it shows four seats bidding nothing and sitting exactly on target for
// the whole of the bidding, which is wrong in the most plausible-looking way.
TEST_CASE("A seat that has not bid shows a dash, not a zero", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    // Ana has bid nothing - genuinely, deliberately. Bogdan has not bid at all.
    snapshot.seats[0].hasBet = true;
    snapshot.seats[0].bet = 0;

    snapshot.seats[1].hasBet = false;
    snapshot.seats[1].bet = 0;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0].bid == QStringLiteral("0"));
    REQUIRE(rows[1].bid != QStringLiteral("0"));
    REQUIRE_FALSE(rows[1].bid.isEmpty());
}

// Section 18 asks for the sign by name. What is read off this column is a delta
// against the total beside it, and a bare "7" does not say which way it went.
TEST_CASE("A scored round carries its sign", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.seats[0].roundScore = 7;
    snapshot.seats[1].roundScore = -1;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].roundScore == QStringLiteral("+7"));
    REQUIRE(rows[1].roundScore == QStringLiteral("-1"));

    // And the sign is available as a NUMBER as well, because the colour is
    // chosen from it - the widget must not be parsing back a '+' it was handed.
    REQUIRE(rows[0].roundScoreSign > 0);
    REQUIRE(rows[1].roundScoreSign < 0);
}

// A round score is zero exactly while the round has not been scored: the
// engine's calculateRoundScore() returns 5 + bid or a negative number and never
// zero. So "+0" is a value this column can never legitimately show, and printing
// it would read as a seat that scored nothing rather than as a round still being
// played - which is what it says for most of every round.
TEST_CASE("A round not yet scored shows no round score at all", "[scoreboard]")
{
    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(fourSeats());

    for(const scoreboard::Row& row : rows)
    {
        INFO("seat " << row.name.toStdString());

        REQUIRE(row.roundScore.isEmpty());
        REQUIRE(row.roundScoreSign == 0);
    }
}

// The engine keeps the round score and the committed total apart, and inside
// onRoundScored() they deliberately disagree - section 16.1's "34 -> 41" is that
// gap drawn. A client that added them would be doing the engine's arithmetic,
// and doing it wrong for one frame a round.
TEST_CASE("The total is the engine's, never the round added to it", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.seats[0].roundScore = 7;
    snapshot.seats[0].totalScore = 34;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].total == QStringLiteral("34"));
    REQUIRE(rows[0].roundScore == QStringLiteral("+7"));
}

TEST_CASE("Every row carries its own seat's numbers", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.seats[2].tricksWon = 3;
    snapshot.seats[2].winStreak = 4;
    snapshot.seats[2].isHuman = true;

    snapshot.seats[3].lossStreak = 2;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    // Order preserved, so the pips cannot land on the row above or below - the
    // off-by-one this whole test exists for.
    REQUIRE(rows[0].name == QStringLiteral("Ana"));
    REQUIRE(rows[3].name == QStringLiteral("Dan"));

    REQUIRE(rows[2].won == QStringLiteral("3"));
    REQUIRE(rows[2].winStreak == 4);
    REQUIRE(rows[2].lossStreak == 0);
    REQUIRE(rows[2].isHuman);

    REQUIRE(rows[3].lossStreak == 2);
    REQUIRE(rows[3].winStreak == 0);
    REQUIRE_FALSE(rows[3].isHuman);

    // bidOrder is 1 for whoever opened the round, and it is the column the eye
    // uses to see who bids next.
    REQUIRE(rows[0].bidOrder == 1);
    REQUIRE(rows[3].bidOrder == 4);
}

// A widget's first paint, before any frame has arrived. MainWindow builds the
// panels in its constructor and the first snapshot does not land until start().
TEST_CASE("A snapshot with no seats produces no rows", "[scoreboard]")
{
    REQUIRE(scoreboard::rowsFor(GameSnapshot()).empty());
}
