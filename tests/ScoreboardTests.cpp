#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "paint/CardRenderer.h"
#include "paint/TextRuns.h"
#include "table/ScoreboardItem.h"

#include <QString>

#include <cmath>
#include <vector>

using romanian_whist::Card;
using romanian_whist::GamePhase;
using romanian_whist::Rank;
using romanian_whist::RoundType;
using romanian_whist::Suit;

// What the scoreboard SAYS (the table redesign's section 2.9), tested without a
// window - and, since phase 8 merged the info strip into it, what the strip said.
//
// The split is SnapshotLog's, and its header states the argument: the formatter
// is the only part of the panel with a right answer, so it is the only part worth
// a test. Whether the columns look right is a thing to look at; whether a seat
// that has not bid reads as having bid nothing is not.
//
// THE ENGLISH IS SAFE TO ASSERT ON, and that is worth saying rather than relying
// on. These strings go through QCoreApplication::translate(), so a loaded
// translation would move them - and TestApp installs no translator, so translate()
// returns its source text. If a suite ever does load one, these assertions are the
// ones that will notice.
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

    for(unsigned int i = 0 ; i < 4 ; i++)
        snapshot.seats[i].seatIndex = i;

    return snapshot;
}

GameSnapshot roundOf(unsigned int number, unsigned int count, unsigned int cards)
{
    GameSnapshot snapshot = fourSeats();

    snapshot.roundNumber = number;
    snapshot.roundCount = count;
    snapshot.trickCount = cards;

    return snapshot;
}

// The bids in, totalling `sum`, in a round of `tricks` - as the snapshot carries
// them.
GameSnapshot bidsIn(unsigned int placed, unsigned int sum, unsigned int tricks)
{
    GameSnapshot snapshot = roundOf(5, 24, tricks);

    snapshot.betsPlaced = placed;
    snapshot.betSum = sum;

    return snapshot;
}

}

// ---- the info strip's words, which the scoreboard says now ----

TEST_CASE("The strip states the round and the number of cards", "[scoreboard]")
{
    const infostrip::Fields fields = infostrip::fieldsFor(roundOf(7, 24, 6));

    REQUIRE(fields.round == QStringLiteral("Round 7 of 24"));

    // The trick count IS the cards dealt to each player, which is why there is
    // no second field for it to disagree with.
    REQUIRE(fields.cards.contains(QStringLiteral("6")));
}

// A one-card round is the first and last round of every schedule, so the
// singular is not a corner case - it is on screen more often than most plurals.
TEST_CASE("One card reads as one card", "[scoreboard]")
{
    // THE SINGULAR ITSELF, not merely that one and two differ - which "%1 cards"
    // also satisfies, and which is what the first version of this test asserted
    // until a mutation run walked straight through it.
    REQUIRE(infostrip::fieldsFor(roundOf(1, 24, 1)).cards == QStringLiteral("1 card"));
    REQUIRE(infostrip::fieldsFor(roundOf(2, 24, 2)).cards == QStringLiteral("2 cards"));

    // Eight is the largest round, and the plural has to survive the whole range
    // rather than just the boundary.
    REQUIRE(infostrip::fieldsFor(roundOf(9, 24, 8)).cards == QStringLiteral("8 cards"));

    // And the scoreboard's own line, which says they were dealt.
    REQUIRE(scoreboard::dealtText(1) == QStringLiteral("1 card dealt"));
    REQUIRE(scoreboard::dealtText(6) == QStringLiteral("6 cards dealt"));
}

// Card's default is a REAL two of clubs, so a scoreboard that draws snapshot.trump
// without checking hasTrump claims a trump in the one round that has none.
// SnapshotLog.cpp carries the same trap and the same comment.
TEST_CASE("The eight-card round has no trump, and none is offered", "[scoreboard]")
{
    GameSnapshot snapshot = roundOf(8, 24, 8);

    snapshot.hasTrump = false;

    // Set deliberately, to prove the guard is on hasTrump rather than on the
    // card looking empty: a default Card is the two of clubs either way.
    snapshot.trump = Card(Rank::Ace, Suit::Spades);

    const infostrip::Fields fields = infostrip::fieldsFor(snapshot);

    REQUIRE_FALSE(fields.hasTrump);
    REQUIRE(fields.trump == Card());

    // And the scoreboard says so, in words, with no card under them.
    const scoreboard::Trump trump = scoreboard::trumpFor(snapshot);

    REQUIRE_FALSE(trump.hasCard);
    REQUIRE(trump.name == QStringLiteral("None this round"));
}

TEST_CASE("A round with a trump carries the card itself", "[scoreboard]")
{
    GameSnapshot snapshot = roundOf(7, 24, 6);

    snapshot.hasTrump = true;
    snapshot.trump = Card(Rank::Queen, Suit::Spades);

    const infostrip::Fields fields = infostrip::fieldsFor(snapshot);

    REQUIRE(fields.hasTrump);
    REQUIRE(fields.trump == Card(Rank::Queen, Suit::Spades));

    const scoreboard::Trump trump = scoreboard::trumpFor(snapshot);

    REQUIRE(trump.hasCard);
    REQUIRE(trump.card == Card(Rank::Queen, Suit::Spades));
}

// The user-facing spelling, which is NOT the log's. snapshotlog prints fixed
// ASCII its own tests assert on and saved logs are read with; this one is read
// by a player and goes through translate(). The client has exactly two switches
// over RoundType, and this is the one for people.
TEST_CASE("The round type is named for a player, not for a log", "[scoreboard]")
{
    GameSnapshot snapshot = roundOf(25, 26, 1);

    snapshot.roundType = RoundType::Forehead;
    REQUIRE(infostrip::fieldsFor(snapshot).roundType == QStringLiteral("Forehead"));

    snapshot.roundType = RoundType::Hidden;
    REQUIRE(infostrip::fieldsFor(snapshot).roundType == QStringLiteral("Hidden"));

    snapshot.roundType = RoundType::Normal;
    REQUIRE(infostrip::fieldsFor(snapshot).roundType == QStringLiteral("Normal"));

    REQUIRE(infostrip::roundTypeText(RoundType::Normal) == QStringLiteral("Normal"));
}

// A default snapshot is what the panel would say before the first frame arrives.
TEST_CASE("A default snapshot states a round of nothing and no trump", "[scoreboard]")
{
    const infostrip::Fields fields = infostrip::fieldsFor(GameSnapshot());

    REQUIRE_FALSE(fields.hasTrump);
    REQUIRE_FALSE(fields.round.isEmpty());
    REQUIRE_FALSE(fields.roundType.isEmpty());
}

TEST_CASE("Forehead and Hidden are called out and Normal is not", "[scoreboard]")
{
    // Section 17's third line: the round type reads in an accent colour, so a
    // blind round is identifiable before the cards are dealt.
    //
    // NEVER A COLOUR-ONLY CUE (section 18), and this case is also what says so:
    // the WORD differs too, and it is the word that carries the information. The
    // colour is reinforcement, which is what makes it safe to use at all.
    REQUIRE(infostrip::isCalledOut(RoundType::Forehead));
    REQUIRE(infostrip::isCalledOut(RoundType::Hidden));
    REQUIRE_FALSE(infostrip::isCalledOut(RoundType::Normal));

    REQUIRE(infostrip::roundTypeText(RoundType::Forehead)
            != infostrip::roundTypeText(RoundType::Normal));

    GameSnapshot snapshot = roundOf(25, 26, 1);
    snapshot.roundType = RoundType::Hidden;

    const scoreboard::Header header = scoreboard::headerFor(snapshot);

    REQUIRE(header.calledOut);
    REQUIRE(header.roundType == QStringLiteral("Hidden"));
    REQUIRE(header.dealt == QStringLiteral("1 card dealt"));
    REQUIRE(header.round == QStringLiteral("Round 25 of 26"));

    snapshot.roundType = RoundType::Normal;
    REQUIRE_FALSE(scoreboard::headerFor(snapshot).calledOut);
}

// ---- how the bids stand ----

TEST_CASE("While anyone is bidding the tension counts the bids so far", "[scoreboard]")
{
    const textruns::Line line = scoreboard::tensionFor(bidsIn(2, 3, 6));

    REQUIRE(textruns::text(line) == QStringLiteral("Bids so far: 3 of 6 tricks"));

    // The total is the part in bold - and only the total.
    REQUIRE(line.size() == 3);
    REQUIRE(line[1] == textruns::Run{ QStringLiteral("3"), textruns::Emphasis::Strong });
    REQUIRE(line[0].emphasis == textruns::Emphasis::Plain);
    REQUIRE(line[2].emphasis == textruns::Emphasis::Plain);
}

TEST_CASE("Once every bid is in the tension says how far over or under", "[scoreboard]")
{
    REQUIRE(textruns::text(scoreboard::tensionFor(bidsIn(4, 7, 6)))
            == QStringLiteral("Bids total 7 for 6 tricks: over by 1"));

    REQUIRE(textruns::text(scoreboard::tensionFor(bidsIn(4, 3, 6)))
            == QStringLiteral("Bids total 3 for 6 tricks: under by 3"));

    // The forbidden bid stops the total landing on the tricks, but the sentence
    // still reads if it ever does.
    REQUIRE(textruns::text(scoreboard::tensionFor(bidsIn(4, 6, 6)))
            == QStringLiteral("Bids total 6 for 6 tricks"));
}

TEST_CASE("A one-trick round reads as one trick", "[scoreboard]")
{
    REQUIRE(textruns::text(scoreboard::tensionFor(bidsIn(1, 0, 1)))
            == QStringLiteral("Bids so far: 0 of 1 trick"));

    REQUIRE(textruns::text(scoreboard::tensionFor(bidsIn(4, 2, 1)))
            == QStringLiteral("Bids total 2 for 1 trick: over by 1"));
}

// ---- the rows ----

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
    REQUIRE(rows[1].bid == QStringLiteral("–"));
}

TEST_CASE("Nobody has taken a trick while the round is being bid", "[scoreboard]")
{
    // A column of zeros under TAKEN would read as a result.
    GameSnapshot snapshot = fourSeats();

    snapshot.phase = GamePhase::Betting;

    for(const scoreboard::Row& row : scoreboard::rowsFor(snapshot))
        REQUIRE(row.taken == QStringLiteral("–"));

    snapshot.phase = GamePhase::Playing;
    snapshot.seats[2].tricksWon = 2;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].taken == QStringLiteral("0"));
    REQUIRE(rows[2].taken == QStringLiteral("2"));
}

TEST_CASE("The rows are in bid order", "[scoreboard]")
{
    // Whoever bids first this round is the top row, as the mock-up's are.
    GameSnapshot snapshot = fourSeats();

    snapshot.seats[0].bidOrder = 3;
    snapshot.seats[1].bidOrder = 4;
    snapshot.seats[2].bidOrder = 1;
    snapshot.seats[3].bidOrder = 2;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].name == QStringLiteral("Cristina"));
    REQUIRE(rows[1].name == QStringLiteral("Dan"));
    REQUIRE(rows[2].name == QStringLiteral("Ana"));
    REQUIRE(rows[3].name == QStringLiteral("Bogdan"));
}

TEST_CASE("A seat whose bid order is not known sorts after the rest by seat", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.seats[0].bidOrder = 0;
    snapshot.seats[1].bidOrder = 2;
    snapshot.seats[2].bidOrder = 0;
    snapshot.seats[3].bidOrder = 1;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].name == QStringLiteral("Dan"));
    REQUIRE(rows[1].name == QStringLiteral("Bogdan"));
    REQUIRE(rows[2].name == QStringLiteral("Ana"));
    REQUIRE(rows[3].name == QStringLiteral("Cristina"));
}

TEST_CASE("The row on turn is marked and no other", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.activeSeat = 2;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[2].turn);
    REQUIRE_FALSE(rows[0].turn);
    REQUIRE_FALSE(rows[1].turn);
    REQUIRE_FALSE(rows[3].turn);

    // -1 is "nobody", between turns: no row is marked.
    snapshot.activeSeat = -1;

    for(const scoreboard::Row& row : scoreboard::rowsFor(snapshot))
        REQUIRE_FALSE(row.turn);
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
    snapshot.seats[1].totalScore = -3;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].points == QStringLiteral("34"));
    REQUIRE(rows[1].points == QStringLiteral("-3"));
}

// THE OWNER'S CALL OF 2026-10-20: the streak stays, as a W or an L and its length.
// The letter carries which way it runs, so the colour is never the only cue.
TEST_CASE("A run reads as W or L and its length", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.seats[0].winStreak = 2;
    snapshot.seats[1].lossStreak = 3;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    REQUIRE(rows[0].run == QStringLiteral("W2"));
    REQUIRE(rows[0].streak == scoreboard::Streak::Winning);

    REQUIRE(rows[1].run == QStringLiteral("L3"));
    REQUIRE(rows[1].streak == scoreboard::Streak::Losing);

    // Neither running: nothing at all, not "W0".
    REQUIRE(rows[2].run.isEmpty());
    REQUIRE(rows[2].streak == scoreboard::Streak::None);
}

TEST_CASE("Every row carries its own seat's numbers", "[scoreboard]")
{
    GameSnapshot snapshot = fourSeats();

    snapshot.phase = GamePhase::Playing;
    snapshot.seats[2].tricksWon = 3;
    snapshot.seats[2].winStreak = 4;
    snapshot.seats[2].totalScore = 27;

    snapshot.seats[3].lossStreak = 2;

    const std::vector<scoreboard::Row> rows = scoreboard::rowsFor(snapshot);

    // Order preserved, so a number cannot land on the row above or below - the
    // off-by-one this whole test exists for.
    REQUIRE(rows[0].name == QStringLiteral("Ana"));
    REQUIRE(rows[3].name == QStringLiteral("Dan"));

    REQUIRE(rows[2].taken == QStringLiteral("3"));
    REQUIRE(rows[2].run == QStringLiteral("W4"));
    REQUIRE(rows[2].points == QStringLiteral("27"));

    REQUIRE(rows[3].run == QStringLiteral("L2"));
}

// A panel's first paint, before any frame has arrived.
TEST_CASE("A snapshot with no seats produces no rows", "[scoreboard]")
{
    REQUIRE(scoreboard::rowsFor(GameSnapshot()).empty());
}

// ---- the trump ----

TEST_CASE("The trump is named under its card with its glyph", "[scoreboard]")
{
    GameSnapshot snapshot = roundOf(7, 24, 6);
    snapshot.hasTrump = true;

    const auto named = [&](Suit suit)
    {
        snapshot.trump = Card(Rank::Two, suit);
        return scoreboard::trumpFor(snapshot).name;
    };

    REQUIRE(named(Suit::Clubs) == QStringLiteral("♣ Clubs"));
    REQUIRE(named(Suit::Spades) == QStringLiteral("♠ Spades"));
    REQUIRE(named(Suit::Hearts) == QStringLiteral("♥ Hearts"));
    REQUIRE(named(Suit::Diamonds) == QStringLiteral("♦ Diamonds"));
}

// ---- the columns ----

// AS WIDE AS ITS COLUMNS, not the mock-up's 236 shared out: the owner's call of
// 2026-10-20, so the panel clears the top-left seat in a narrow window.
TEST_CASE("The panel is as wide as its columns", "[scoreboard]")
{
    const scoreboard::Columns columns = scoreboard::columnsFor(scoreboard::rowsFor(fourSeats()));

    REQUIRE(columns.run > 0.0);

    const CardRenderer cards;
    const QSizeF size = scoreboard::sizeFor(scoreboard::shownFor(fourSeats()), cards);

    // The columns, and the border and padding round them - narrower than the
    // mock-up, for names this short.
    REQUIRE(std::abs(size.width() - (columns.sum() + 30.0)) < 1e-6);
    REQUIRE(size.width() < scoreboard::mockupWidth);
}

TEST_CASE("A longer name widens the panel, up to the mock-up's width", "[scoreboard]")
{
    const auto widthWith = [](const char* name)
    {
        GameSnapshot snapshot = fourSeats();
        snapshot.seats[2].name = name;

        return scoreboard::columnsFor(scoreboard::rowsFor(snapshot)).sum() + 30.0;
    };

    const qreal shorter = widthWith("Cris");
    const qreal longer = widthWith("Alexandru");
    const qreal tooLong = widthWith("Alexandrina-Maria Constantinescu");

    REQUIRE(longer > shorter);
    REQUIRE(std::abs(tooLong - scoreboard::mockupWidth) < 1e-6);
}

// THE TENSION AT ITS WIDEST SETS THE LEAST WIDTH: with names as short as they come
// the columns are narrower than its longest sentence, which would otherwise wrap
// as the bids came in and push everything under it down a line.
TEST_CASE("The panel keeps its height as the bids come in", "[scoreboard]")
{
    const CardRenderer cards;

    const auto heightFor = [&cards](unsigned int placed, unsigned int sum)
    {
        GameSnapshot snapshot = bidsIn(placed, sum, 8);

        for(SeatView& seat : snapshot.seats)
            seat.name = "A";

        return scoreboard::sizeFor(scoreboard::shownFor(snapshot), cards).height();
    };

    // So far, then even, under by all eight, and over by as much as four seats
    // can bid.
    const qreal bidding = heightFor(2, 3);

    REQUIRE(heightFor(4, 8) == bidding);
    REQUIRE(heightFor(4, 0) == bidding);
    REQUIRE(heightFor(4, 32) == bidding);
}

TEST_CASE("The columns hold still as the turn and the scores move", "[scoreboard]")
{
    // Measured from the names as though each carried the turn's mark, and the
    // numbers at their widest: a turn passing, or a score growing from 9 to 10,
    // moves nothing.
    GameSnapshot snapshot = fourSeats();

    const scoreboard::Columns before = scoreboard::columnsFor(scoreboard::rowsFor(snapshot));

    snapshot.activeSeat = 2;
    snapshot.seats[0].totalScore = 134;
    snapshot.seats[1].winStreak = 4;
    snapshot.seats[3].hasBet = true;
    snapshot.seats[3].bet = 8;

    REQUIRE(scoreboard::columnsFor(scoreboard::rowsFor(snapshot)) == before);
}

TEST_CASE("Names too long for the panel are what gives way", "[scoreboard]")
{
    // Every number column keeps its widest content, and the names take what is
    // left - elided when the panel draws them - rather than pushing the panel
    // wider or the numbers together.
    GameSnapshot snapshot = fourSeats();
    snapshot.seats[2].name = "Alexandrina-Maria Constantinescu";

    const scoreboard::Columns plain = scoreboard::columnsFor(scoreboard::rowsFor(fourSeats()));
    const scoreboard::Columns longer = scoreboard::columnsFor(scoreboard::rowsFor(snapshot));

    REQUIRE(longer.player > 0.0);
    REQUIRE(longer.bid <= plain.bid);
    REQUIRE(std::abs(longer.sum() - (scoreboard::mockupWidth - 30.0)) < 1e-6);
}

TEST_CASE("The panel is taller with the trump's card than without it", "[scoreboard]")
{
    const CardRenderer cards;

    GameSnapshot snapshot = roundOf(7, 24, 6);
    snapshot.hasTrump = true;
    snapshot.trump = Card(Rank::Queen, Suit::Clubs);

    const QSizeF withCard = scoreboard::sizeFor(scoreboard::shownFor(snapshot), cards);

    snapshot.hasTrump = false;

    const QSizeF without = scoreboard::sizeFor(scoreboard::shownFor(snapshot), cards);

    // By the card - a hand card's height inside a panel drawn 1.2 times the table
    // - and the gap after it.
    REQUIRE(std::abs(withCard.height() - without.height() - (225.0 / 1.2 + 8.0)) < 1e-6);

    // And a row more is a row taller.
    snapshot.seats.push_back(seatWith("Elena", 5));

    REQUIRE(scoreboard::sizeFor(scoreboard::shownFor(snapshot), cards).height() > without.height());
}
