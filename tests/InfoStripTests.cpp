#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "ui/InfoStrip.h"

#include <QFontMetricsF>
#include <QString>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::RoundType;
using romanian_whist::Suit;

// The four facts the strip states, tested without a window.
//
// THE ENGLISH IS SAFE TO ASSERT ON, and that is worth saying rather than
// relying on. These strings go through QCoreApplication::translate(), so a
// loaded translation would move them - and TestApp installs no translator, so
// translate() returns its source text. If a suite ever does load one, these
// assertions are the ones that will notice.
namespace
{

GameSnapshot roundOf(unsigned int number, unsigned int count, unsigned int cards)
{
    GameSnapshot snapshot;

    snapshot.roundNumber = number;
    snapshot.roundCount = count;
    snapshot.trickCount = cards;

    return snapshot;
}

}

TEST_CASE("The strip states the round and the number of cards", "[info-strip]")
{
    const infostrip::Fields fields = infostrip::fieldsFor(roundOf(7, 24, 6));

    REQUIRE(fields.round == QStringLiteral("Round 7 of 24"));

    // The trick count IS the cards dealt to each player, which is why there is
    // no second field for it to disagree with.
    REQUIRE(fields.cards.contains(QStringLiteral("6")));
}

// A one-card round is the first and last round of every schedule, so the
// singular is not a corner case - it is on screen more often than most plurals.
TEST_CASE("One card reads as one card", "[info-strip]")
{
    // THE SINGULAR ITSELF, not merely that one and two differ - which "%1 cards"
    // also satisfies, and which is what the first version of this test asserted
    // until a mutation run walked straight through it.
    REQUIRE(infostrip::fieldsFor(roundOf(1, 24, 1)).cards == QStringLiteral("1 card"));
    REQUIRE(infostrip::fieldsFor(roundOf(2, 24, 2)).cards == QStringLiteral("2 cards"));

    // Eight is the largest round, and the plural has to survive the whole range
    // rather than just the boundary.
    REQUIRE(infostrip::fieldsFor(roundOf(9, 24, 8)).cards == QStringLiteral("8 cards"));
}

// Card's default is a REAL two of clubs, so a strip that draws snapshot.trump
// without checking hasTrump claims a trump in the one round that has none.
// SnapshotLog.cpp carries the same trap and the same comment.
TEST_CASE("The eight-card round has no trump, and none is offered", "[info-strip]")
{
    GameSnapshot snapshot = roundOf(8, 24, 8);

    snapshot.hasTrump = false;

    // Set deliberately, to prove the guard is on hasTrump rather than on the
    // card looking empty: a default Card is the two of clubs either way.
    snapshot.trump = Card(Rank::Ace, Suit::Spades);

    const infostrip::Fields fields = infostrip::fieldsFor(snapshot);

    REQUIRE_FALSE(fields.hasTrump);
    REQUIRE(fields.trump == Card());
}

TEST_CASE("A round with a trump carries the card itself", "[info-strip]")
{
    GameSnapshot snapshot = roundOf(7, 24, 6);

    snapshot.hasTrump = true;
    snapshot.trump = Card(Rank::Queen, Suit::Spades);

    const infostrip::Fields fields = infostrip::fieldsFor(snapshot);

    REQUIRE(fields.hasTrump);
    REQUIRE(fields.trump == Card(Rank::Queen, Suit::Spades));
}

// The user-facing spelling, which is NOT the log's. snapshotlog prints fixed
// ASCII its own tests assert on and saved logs are read with; this one is read
// by a player and goes through translate(). The client has exactly two switches
// over RoundType, and this is the one for people - MainWindow's status bar was a
// third until B2b and now calls this.
TEST_CASE("The round type is named for a player, not for a log", "[info-strip]")
{
    GameSnapshot snapshot = roundOf(25, 26, 1);

    snapshot.roundType = RoundType::Forehead;
    REQUIRE(infostrip::fieldsFor(snapshot).roundType == QStringLiteral("Forehead"));

    snapshot.roundType = RoundType::Hidden;
    REQUIRE(infostrip::fieldsFor(snapshot).roundType == QStringLiteral("Hidden"));

    snapshot.roundType = RoundType::Normal;
    REQUIRE(infostrip::fieldsFor(snapshot).roundType == QStringLiteral("Normal"));

    // Capitalised, unlike the status bar's old lowercase words - these are
    // labels in a strip rather than a fragment inside a sentence.
    REQUIRE(infostrip::roundTypeText(RoundType::Normal) == QStringLiteral("Normal"));
}

// THE ONE ASSERTION HERE THAT NEEDS A WIDGET, and the reason is that this is
// geometry rather than wording - the split the header of ScoreboardTests.cpp
// argues for, on the side where there IS a right answer. A strip whose height
// depends on the round moves everything laid out beneath it: hiding the trump
// label took the strip from 77 pixels to 29, so the table and the scoreboard
// jumped some fifty pixels on entering the eight-card round and back on leaving
// it, twice in every game. GameBridgeTests proves real games produce those
// frames.
//
// It constructs a QWidget, which the suite can do because TestApp is a
// QApplication on the offscreen platform - see the type's history there.
TEST_CASE("The strip is the same height with and without a trump", "[info-strip]")
{
    InfoStrip strip;

    GameSnapshot withTrump = roundOf(7, 24, 6);

    withTrump.hasTrump = true;
    withTrump.trump = Card(Rank::Queen, Suit::Spades);

    strip.setSnapshot(withTrump);

    const int tall = strip.sizeHint().height();

    // Card-sized rather than text-sized, asserted separately because the
    // equality below would hold just as well if the strip had collapsed in both
    // rounds.
    REQUIRE(tall > qRound(QFontMetricsF(strip.font()).height() * 2.0));

    GameSnapshot noTrump = roundOf(8, 24, 8);

    noTrump.hasTrump = false;

    strip.setSnapshot(noTrump);

    REQUIRE(strip.sizeHint().height() == tall);
}

// A default snapshot is what the strip holds before the first frame arrives.
TEST_CASE("A default snapshot states a round of nothing and no trump", "[info-strip]")
{
    const infostrip::Fields fields = infostrip::fieldsFor(GameSnapshot());

    REQUIRE_FALSE(fields.hasTrump);
    REQUIRE_FALSE(fields.round.isEmpty());
    REQUIRE_FALSE(fields.roundType.isEmpty());
}

TEST_CASE("Forehead and Hidden are called out and Normal is not", "[info-strip]")
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
}
