#include <catch2/catch_test_macros.hpp>

#include "ui/PromptBar.h"

#include <QString>

#include <vector>

using namespace romanian_whist;

// Section 12's fourth region: the line that says WHY most of your hand is grey.
//
// THE ENGLISH IS SAFE TO ASSERT ON for the reason InfoStripTests states: these
// go through QCoreApplication::translate() and TestApp installs no translator.

namespace
{
PlayPrompt promptOf(std::vector<bool> legality)
{
    PlayPrompt prompt;
    prompt.promptId = 7;

    const Card card(Rank::Ace, Suit::Spades);

    for(bool legal : legality)
        prompt.entries.push_back(HandEntry{ card, legal });

    return prompt;
}

GameSnapshot leadingWith(Suit suit)
{
    GameSnapshot snapshot;
    snapshot.hasLeadSuit = true;
    snapshot.leadSuit = suit;

    return snapshot;
}
}

TEST_CASE("A restricted hand is told which suit to follow", "[prompt-bar]")
{
    const promptbar::Fields fields =
        promptbar::fieldsFor(promptOf({ true, false, false }), leadingWith(Suit::Spades));

    REQUIRE(fields.hint.contains(QStringLiteral("♠")));
}

TEST_CASE("Leading a trick says so", "[prompt-bar]")
{
    // Nothing played yet, so there is no suit to follow and every card is legal.
    GameSnapshot snapshot;

    REQUIRE_FALSE(snapshot.hasLeadSuit);

    const promptbar::Fields fields =
        promptbar::fieldsFor(promptOf({ true, true, true }), snapshot);

    REQUIRE(fields.hint.contains(QStringLiteral("lead")));
}

TEST_CASE("A void hand is not told to follow a suit it does not hold", "[prompt-bar]")
{
    // THE CASE THE ENTRIES EXIST FOR, and the one a bar driven off the snapshot
    // alone gets wrong. There IS a lead suit - hasLeadSuit is true - and the
    // player is void in it, so every card is legal and nothing is being
    // restricted. Telling them to follow spades here is telling them to do
    // something they cannot.
    const promptbar::Fields fields =
        promptbar::fieldsFor(promptOf({ true, true, true }), leadingWith(Suit::Spades));

    REQUIRE_FALSE(fields.hint.contains(QStringLiteral("follow")));

    // And not "You lead" either, because they are not leading - a trick is
    // already under way.
    REQUIRE(fields.hint.isEmpty());
}

TEST_CASE("A blind round explains nothing, because there is nothing to choose", "[prompt-bar]")
{
    // One card, face down, forced. A bar saying "you must follow" would be
    // describing a choice the player does not have.
    PlayPrompt prompt;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    REQUIRE(promptbar::fieldsFor(prompt, leadingWith(Suit::Hearts)).hint.isEmpty());
}

TEST_CASE("Between turns the bar says nothing", "[prompt-bar]")
{
    REQUIRE(promptbar::idle().hint.isEmpty());
}

TEST_CASE("The bar keeps its height when it has nothing to say", "[prompt-bar]")
{
    // B2b's finding, guarded rather than rediscovered: a widget whose size
    // depends on what it is saying moves everything laid out above it. This bar
    // is empty for most of a game and would otherwise collapse and reopen twice
    // a trick, where the trump label managed it twice a game.
    PromptBar bar;

    const int empty = bar.sizeHint().height();

    bar.setFields(promptbar::fieldsFor(promptOf({ true, false }), leadingWith(Suit::Clubs)));

    REQUIRE(bar.sizeHint().height() == empty);
}
