#include <catch2/catch_test_macros.hpp>

#include "GameFixtures.h"

#include "ui/BidStrip.h"

#include <QObject>
#include <QPushButton>
#include <QString>

#include <vector>

#include <cstddef>
#include <optional>

// Section 15.1's strip, in two halves.
//
// FIRST, fieldsFor() - the part with a right answer, tested without a window,
// which is the split InfoStrip and SnapshotLog both argue for. THEN the widget
// itself, which the suite can construct because TestApp is a QApplication on the
// offscreen platform.
//
// THE ENGLISH IS SAFE TO ASSERT ON for the reason InfoStripTests states: these
// strings go through QCoreApplication::translate() and TestApp installs no
// translator, so translate() returns its source text.

namespace
{
BetPrompt promptFor(std::size_t handSize, std::optional<unsigned int> forbidden = std::nullopt)
{
    BetPrompt prompt;
    prompt.promptId = 7;
    prompt.handSize = handSize;
    prompt.forbiddenBet = forbidden;

    return prompt;
}

// How many bid buttons the strip is currently offering. Found by type rather
// than tracked, so the test is asking the widget what it built rather than
// believing what it was told.
std::size_t buttonCount(const BidStrip& strip)
{
    return static_cast<std::size_t>(strip.findChildren<QPushButton*>().size());
}

QPushButton* buttonFor(const BidStrip& strip, const QString& text)
{
    for(QPushButton* button : strip.findChildren<QPushButton*>())
    {
        if(button->text() == text)
            return button;
    }

    return nullptr;
}
}

// ---- fieldsFor(), no widget ----

TEST_CASE("The strip offers every bid from zero to the hand size", "[bid-strip]")
{
    // INCLUSIVE at both ends, which is the off-by-one to get right: an 8-card
    // round has nine legal bids, because bidding zero is a real choice rather
    // than the absence of one.
    const bidstrip::Fields fields = bidstrip::fieldsFor(promptFor(8));

    REQUIRE(fields.highest == 8);
    REQUIRE_FALSE(fields.forbidden.has_value());

    // Nothing to explain when nothing is disabled, and empty is what the widget
    // hides the label on.
    REQUIRE(fields.explanation.isEmpty());
    REQUIRE_FALSE(fields.question.isEmpty());
}

TEST_CASE("A forbidden bid is carried with the reason for it", "[bid-strip]")
{
    const bidstrip::Fields fields = bidstrip::fieldsFor(promptFor(6, 3u));

    REQUIRE(fields.forbidden == 3u);

    // BOTH NUMBERS, because either alone is unreadable: "3 is not allowed" does
    // not say why, and "the bids would add up to 6" does not say which bid.
    REQUIRE(fields.explanation.contains("3"));
    REQUIRE(fields.explanation.contains("6"));
}

TEST_CASE("A forbidden bid of zero is still a forbidden bid", "[bid-strip]")
{
    // THE OFF-BY-ONE THIS EXISTS TO CATCH: zero is a legal bid, so an optional
    // holding 0 must not be read as "nothing forbidden". A strip that tested the
    // value rather than the engagement would leave the one illegal button live
    // in exactly the round where the bids are tightest.
    const bidstrip::Fields fields = bidstrip::fieldsFor(promptFor(4, 0u));

    REQUIRE(fields.forbidden == 0u);
    REQUIRE_FALSE(fields.explanation.isEmpty());
}

TEST_CASE("A blind round's strip is built from the count, never from a card", "[bid-strip]")
{
    // Forehead and Hidden are 1-trick rounds, so the strip is 0 and 1 - and
    // BetPrompt::hand is EMPTY there, because the bidder is not entitled to
    // their own hand. Fields carries no card of any kind, which is what makes
    // that leak impossible here rather than merely avoided.
    BetPrompt prompt = promptFor(1);
    prompt.roundType = romanian_whist::RoundType::Hidden;

    REQUIRE(prompt.hand.empty());

    const bidstrip::Fields fields = bidstrip::fieldsFor(prompt);

    REQUIRE(fields.highest == 1);
}

// ---- the widget ----

TEST_CASE("The strip builds one button per bid", "[bid-strip]")
{
    BidStrip strip;
    strip.setPrompt(promptFor(8));

    REQUIRE(buttonCount(strip) == 9);
    REQUIRE(buttonFor(strip, "0") != nullptr);
    REQUIRE(buttonFor(strip, "8") != nullptr);
}

TEST_CASE("The forbidden button is disabled and no other is", "[bid-strip]")
{
    // The assertion the panel exists for. QtMoveProvider::makeBet() re-prompts
    // on an illegal bet rather than passing it to the engine, so missing this is
    // a strip that reappears rather than a crash - but the player is told
    // nothing about why.
    BidStrip strip;
    strip.setPrompt(promptFor(6, 3u));

    for(QPushButton* button : strip.findChildren<QPushButton*>())
        REQUIRE(button->isEnabled() == (button->text() != QStringLiteral("3")));
}

TEST_CASE("Clicking a button reports that button's bid", "[bid-strip]")
{
    // THE BID, NOT THE POSITION. They are equal here because the buttons run
    // 0..highest with none missing - which is exactly what makes the mistake
    // invisible - and it is the same class of error as submitting a display slot
    // where the engine wants a hand position.
    BidStrip strip;
    strip.setPrompt(promptFor(6, 3u));

    // A lambda rather than QSignalSpy, which lives in Qt6::Test and would be a
    // whole module linked for one assertion. Every other suite here records
    // signals this way.
    std::vector<unsigned int> chosen;

    QObject::connect(&strip, &BidStrip::bidChosen,
                     [&chosen](unsigned int bet){ chosen.push_back(bet); });

    QPushButton* five = buttonFor(strip, "5");
    REQUIRE(five != nullptr);

    five->click();

    REQUIRE(chosen.size() == 1);
    REQUIRE(chosen.front() == 5u);
}

TEST_CASE("A second prompt leaves no buttons from the first", "[bid-strip]")
{
    // The schedule runs 1 card up to 8 and back down, so the strip is rebuilt
    // every round and most rebuilds are SMALLER than the last. Hidden buttons
    // still belong to the layout, so a strip that hid rather than deleted would
    // offer bids the round does not have.
    BidStrip strip;
    strip.setPrompt(promptFor(8));

    REQUIRE(buttonCount(strip) == 9);

    strip.setPrompt(promptFor(1));

    REQUIRE(buttonCount(strip) == 2);
    REQUIRE(buttonFor(strip, "8") == nullptr);
}

// ---- the answer the seated-game tests bid with ----
//
// These three moved here from PromptStandInTests in C2, with the function they
// cover: BidStrip took chooseBet()'s only production caller, so it became
// fixtures::lowestLegalBid(). They are still worth keeping - HumanSeatTests
// answers every bid of three whole games with it, and a wrong answer there is a
// re-prompt loop rather than a failure.

TEST_CASE("The tests' own bidder bids zero when zero is allowed", "[bid-strip]")
{
    REQUIRE(fixtures::lowestLegalBid(promptFor(8, 3u)) == 0);
}

TEST_CASE("The tests' own bidder bids one when zero is forbidden", "[bid-strip]")
{
    // The whole reason it is a function rather than a literal 0.
    REQUIRE(fixtures::lowestLegalBid(promptFor(8, 0u)) == 1);
}

TEST_CASE("The tests' own bidder bids zero when nothing is forbidden", "[bid-strip]")
{
    REQUIRE(fixtures::lowestLegalBid(promptFor(1)) == 0);
}
