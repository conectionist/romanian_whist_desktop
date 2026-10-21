#include <catch2/catch_test_macros.hpp>

#include "paint/TextRuns.h"
#include "table/TrickPanelItem.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/GameEngine.h>

#include <QString>

#include <memory>
#include <vector>

using namespace romanian_whist;

// SECTION 2.10's TRICK PANEL: which trick, who led it, who is winning, and under a
// rule the line where the game talks to you - section 12's fourth region, the
// prompt bar's until the table redesign's phase 8. Above all, the line that says
// WHY most of your hand is dimmed.
//
// THE ENGLISH IS SAFE TO ASSERT ON for the reason ScoreboardTests states: these go
// through QCoreApplication::translate() and TestApp installs no translator.

namespace
{
// A prompt whose hand is the Ace of Spades once per entry, legal or not.
PlayPrompt promptOf(std::vector<bool> legality)
{
    PlayPrompt prompt;
    prompt.promptId = 7;

    const Card card(Rank::Ace, Suit::Spades);

    for(bool legal : legality)
        prompt.entries.push_back(HandEntry{ card, legal });

    return prompt;
}

// A prompt whose hand is these cards, legal or not.
PlayPrompt promptOf(std::vector<HandEntry> entries)
{
    PlayPrompt prompt;
    prompt.promptId = 7;
    prompt.entries = std::move(entries);

    return prompt;
}

GameSnapshot leadingWith(Suit suit)
{
    GameSnapshot snapshot;
    snapshot.hasLeadSuit = true;
    snapshot.leadSuit = suit;

    return snapshot;
}

// Four seats, you in seat 0, in the play phase: Bogdan led the trick and his King
// of Hearts is winning it.
GameSnapshot playing()
{
    GameSnapshot snapshot;

    snapshot.humanSeat = 0;
    snapshot.phase = GamePhase::Playing;
    snapshot.trickCount = 6;
    snapshot.trickNumber = 3;
    snapshot.leadSeat = 1;
    snapshot.hasLeadSuit = true;
    snapshot.leadSuit = Suit::Hearts;
    snapshot.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Hearts), true } };

    const char* const names[] = { "Player", "Bogdan", "Ana", "Costel" };

    for(unsigned int i = 0 ; i < 4 ; i++)
    {
        SeatView seat;
        seat.name = names[i];
        seat.seatIndex = i;
        seat.isHuman = i == 0;
        seat.bidOrder = (i + 3) % 4 + 1;
        snapshot.seats.push_back(seat);
    }

    return snapshot;
}

QString promptOf(const trickpanel::Fields& fields)
{
    return textruns::text(fields.prompt);
}

QString rowOf(const trickpanel::Fields& fields, std::size_t row)
{
    return row < fields.rows.size() ? textruns::text(fields.rows[row]) : QString();
}
}

// ---- your turn: why the others are out ----

TEST_CASE("A restricted hand is told which suit to follow", "[trick-panel]")
{
    const PlayPrompt prompt = promptOf({ HandEntry{ Card(Rank::Two, Suit::Spades), true },
                                         HandEntry{ Card(Rank::Ace, Suit::Hearts), false } });

    REQUIRE(trickpanel::yourTurn(prompt, leadingWith(Suit::Spades))
            == QStringLiteral("Your turn. You must follow ♠."));
}

TEST_CASE("Leading a trick says so", "[trick-panel]")
{
    // Nothing played yet, so there is no suit to follow and every card is legal.
    GameSnapshot snapshot;

    REQUIRE_FALSE(snapshot.hasLeadSuit);
    REQUIRE(trickpanel::yourTurn(promptOf({ true, true, true }), snapshot)
            == QStringLiteral("Your turn. You lead."));
}

TEST_CASE("A void hand is not told to follow a suit it does not hold", "[trick-panel]")
{
    // THE CASE THE ENTRIES EXIST FOR, and the one a line driven off the snapshot
    // alone gets wrong. There IS a lead suit - hasLeadSuit is true - and the
    // player is void in it with no trump, so every card is legal and nothing is
    // being restricted. Telling them to follow spades here is telling them to do
    // something they cannot - and they are not leading either.
    const QString line = trickpanel::yourTurn(promptOf({ true, true, true }), leadingWith(Suit::Spades));

    REQUIRE_FALSE(line.contains(QStringLiteral("follow")));
    REQUIRE(line == QStringLiteral("Your turn. Any card."));
}

TEST_CASE("A hand void in the lead suit is told it must trump", "[trick-panel]")
{
    // RULES.md: "Must play trump if holding no card of the lead suit." The legal
    // cards are the trumps, none of them hearts - so the line names the suit the
    // player cannot follow, not one they can.
    const PlayPrompt prompt = promptOf({ HandEntry{ Card(Rank::Two, Suit::Clubs), true },
                                         HandEntry{ Card(Rank::Ace, Suit::Spades), false } });

    REQUIRE(trickpanel::yourTurn(prompt, leadingWith(Suit::Hearts))
            == QStringLiteral("Your turn. No ♥, you must trump."));
}

TEST_CASE("A blind round explains nothing, because there is nothing to choose", "[trick-panel]")
{
    // One card, face down, forced. "You must follow" would be describing a choice
    // the player does not have.
    PlayPrompt prompt;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    REQUIRE(trickpanel::yourTurn(prompt, leadingWith(Suit::Hearts)) == QStringLiteral("Your turn."));
}

// ---- the panel ----

TEST_CASE("The play panel names the trick, its leader and its winner", "[trick-panel]")
{
    const trickpanel::Fields fields = trickpanel::fieldsFor(playing(), nullptr);

    REQUIRE(fields.title == QStringLiteral("Trick 3 of 6"));
    REQUIRE(rowOf(fields, 0) == QStringLiteral("Leader: Bogdan · Led ♥"));
    REQUIRE(rowOf(fields, 1) == QStringLiteral("Winning: Bogdan, K♥"));

    // The names and the card stand out; the labels do not.
    REQUIRE(fields.rows[0][1] == textruns::Run{ QStringLiteral("Bogdan"), textruns::Emphasis::Strong });
    REQUIRE(fields.rows[0][0].emphasis == textruns::Emphasis::Plain);
}

TEST_CASE("Before the first card the leader is named and nobody is winning", "[trick-panel]")
{
    GameSnapshot snapshot = playing();

    snapshot.table.clear();
    snapshot.hasLeadSuit = false;
    snapshot.leadSeat = 2;

    const trickpanel::Fields fields = trickpanel::fieldsFor(snapshot, nullptr);

    // No suit led yet, so no "Led".
    REQUIRE(rowOf(fields, 0) == QStringLiteral("Leader: Ana"));
    REQUIRE(rowOf(fields, 1) == QStringLiteral("Winning: –"));
}

TEST_CASE("Your own seat is You in every sentence", "[trick-panel]")
{
    GameSnapshot snapshot = playing();

    snapshot.table = { PlayedCardView{ 0, Card(Rank::Ace, Suit::Hearts), true } };

    const trickpanel::Fields fields = trickpanel::fieldsFor(snapshot, nullptr);

    REQUIRE(rowOf(fields, 0) == QStringLiteral("Leader: You · Led ♥"));
    REQUIRE(rowOf(fields, 1) == QStringLiteral("Winning: You, A♥"));
}

TEST_CASE("The prompt follows whose move it is", "[trick-panel]")
{
    GameSnapshot snapshot = playing();

    // A bot on turn.
    snapshot.activeSeat = 2;
    REQUIRE(promptOf(trickpanel::fieldsFor(snapshot, nullptr)) == QStringLiteral("Ana is thinking…"));

    // Yours, before the prompt has arrived.
    snapshot.activeSeat = 0;
    REQUIRE(promptOf(trickpanel::fieldsFor(snapshot, nullptr)) == QStringLiteral("Your turn."));

    // And once it has: the reason the dimmed cards are out.
    const PlayPrompt prompt = promptOf({ HandEntry{ Card(Rank::Two, Suit::Hearts), true },
                                         HandEntry{ Card(Rank::Ace, Suit::Spades), false } });

    REQUIRE(promptOf(trickpanel::fieldsFor(snapshot, &prompt))
            == QStringLiteral("Your turn. You must follow ♥."));

    // Between turns, nothing - and the panel keeps the line for it.
    snapshot.activeSeat = -1;
    REQUIRE(trickpanel::fieldsFor(snapshot, nullptr).prompt.empty());
}

TEST_CASE("A full trick says who won it", "[trick-panel]")
{
    GameSnapshot snapshot = playing();

    snapshot.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Hearts) },
                       PlayedCardView{ 2, Card(Rank::Two, Suit::Hearts) },
                       PlayedCardView{ 3, Card(Rank::Ace, Suit::Hearts), true },
                       PlayedCardView{ 0, Card(Rank::Three, Suit::Hearts) } };

    REQUIRE(promptOf(trickpanel::fieldsFor(snapshot, nullptr)) == QStringLiteral("Costel wins the trick."));

    snapshot.table[2].isWinning = false;
    snapshot.table[3].isWinning = true;

    REQUIRE(promptOf(trickpanel::fieldsFor(snapshot, nullptr)) == QStringLiteral("You win the trick."));
}

TEST_CASE("The bidding panel names the first bidder and who is bidding", "[trick-panel]")
{
    GameSnapshot snapshot = playing();

    snapshot.phase = GamePhase::Betting;
    snapshot.table.clear();
    snapshot.activeSeat = 3;

    trickpanel::Fields fields = trickpanel::fieldsFor(snapshot, nullptr);

    REQUIRE(fields.title == QStringLiteral("Bidding"));
    REQUIRE(rowOf(fields, 0) == QStringLiteral("First to bid: Bogdan"));
    REQUIRE(promptOf(fields) == QStringLiteral("Costel is thinking…"));

    snapshot.activeSeat = 0;
    fields = trickpanel::fieldsFor(snapshot, nullptr);

    REQUIRE(promptOf(fields) == QStringLiteral("Your bid. How many tricks?"));
}

TEST_CASE("A scored round and a finished game say so", "[trick-panel]")
{
    GameSnapshot snapshot = playing();

    snapshot.phase = GamePhase::RoundScored;
    trickpanel::Fields fields = trickpanel::fieldsFor(snapshot, nullptr);

    REQUIRE(fields.title == QStringLiteral("Round over"));
    REQUIRE(promptOf(fields) == QStringLiteral("Scores are in."));
    REQUIRE(fields.rows.empty());

    snapshot.phase = GamePhase::GameOver;
    fields = trickpanel::fieldsFor(snapshot, nullptr);

    REQUIRE(fields.title == QStringLiteral("Game over"));
}

TEST_CASE("A watched game has nobody called You", "[trick-panel]")
{
    // A spectator has no seat: seat 0 is a player like any other.
    GameSnapshot snapshot = playing();

    snapshot.humanSeat = -1;
    snapshot.activeSeat = 0;
    snapshot.table = { PlayedCardView{ 0, Card(Rank::Ace, Suit::Hearts), true } };

    const trickpanel::Fields fields = trickpanel::fieldsFor(snapshot, nullptr);

    REQUIRE(rowOf(fields, 0) == QStringLiteral("Leader: Player · Led ♥"));
    REQUIRE(promptOf(fields) == QStringLiteral("Player is thinking…"));
}

TEST_CASE("The trick before the first is the first", "[trick-panel]")
{
    // Between the last bid and the first card the engine reports trick 0.
    GameSnapshot snapshot = playing();
    snapshot.trickNumber = 0;

    REQUIRE(trickpanel::fieldsFor(snapshot, nullptr).title == QStringLiteral("Trick 1 of 6"));
}

TEST_CASE("The panel keeps its height between turns", "[trick-panel]")
{
    // The prompt keeps its line when it has nothing to say, so the panel does not
    // jump on every move - B2b's finding for the prompt bar, which this replaces.
    GameSnapshot snapshot = playing();

    snapshot.activeSeat = 2;
    const QSizeF asked = trickpanel::sizeFor(trickpanel::fieldsFor(snapshot, nullptr));

    snapshot.activeSeat = -1;
    const QSizeF idle = trickpanel::sizeFor(trickpanel::fieldsFor(snapshot, nullptr));

    REQUIRE(idle == asked);
    REQUIRE(idle.width() == trickpanel::width);
}

// AND NOTHING ON IT EVER TAKES A SECOND LINE (PR 34's review, finding #2): the two
// longest prompts used to wrap, the bid on every one of yours, and so did a line
// with a long name in it - a panel a line taller for as long as each showed.
TEST_CASE("The panel keeps its height through the longest lines it says", "[trick-panel]")
{
    // The setup dialog's longest, 20 characters, for everyone but you.
    GameSnapshot snapshot = playing();

    for(SeatView& seat : snapshot.seats)
    {
        if(!seat.isHuman)
            seat.name = "Bartholomew Feathers";
    }

    const auto height = [](const GameSnapshot& at, const PlayPrompt* prompt = nullptr)
    {
        return trickpanel::sizeFor(trickpanel::fieldsFor(at, prompt)).height();
    };

    // THE PLAY, a long name leading and winning: idle, then a long name thinking,
    // then you void in hearts and made to trump.
    snapshot.activeSeat = -1;
    const qreal play = height(snapshot);

    snapshot.activeSeat = 2;
    REQUIRE(height(snapshot) == play);

    const PlayPrompt trump = promptOf({ HandEntry{ Card(Rank::Two, Suit::Clubs), true },
                                        HandEntry{ Card(Rank::Ace, Suit::Spades), false } });

    snapshot.activeSeat = 0;
    REQUIRE(height(snapshot, &trump) == play);

    // A full trick a long name won.
    GameSnapshot won = snapshot;
    won.activeSeat = -1;
    won.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Hearts) },
                  PlayedCardView{ 2, Card(Rank::Two, Suit::Hearts) },
                  PlayedCardView{ 3, Card(Rank::Ace, Suit::Hearts), true },
                  PlayedCardView{ 0, Card(Rank::Three, Suit::Hearts) } };

    REQUIRE(height(won) == play);

    // THE BIDDING, a long name first to bid: your bid, a long name thinking, idle.
    snapshot.phase = GamePhase::Betting;
    snapshot.table.clear();

    snapshot.activeSeat = 0;
    const qreal bidding = height(snapshot);

    snapshot.activeSeat = 3;
    REQUIRE(height(snapshot) == bidding);

    snapshot.activeSeat = -1;
    REQUIRE(height(snapshot) == bidding);
}

TEST_CASE("A long name gives way, and the rest of its line stays", "[trick-panel]")
{
    GameSnapshot snapshot = playing();
    snapshot.seats[1].name = "Bartholomew Feathers";
    snapshot.seats[3].name = "Bartholomew Feathers";

    snapshot.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Hearts) },
                       PlayedCardView{ 2, Card(Rank::Two, Suit::Hearts) },
                       PlayedCardView{ 3, Card(Rank::Ace, Suit::Hearts), true },
                       PlayedCardView{ 0, Card(Rank::Three, Suit::Hearts) } };

    const trickpanel::Fields fields = trickpanel::fieldsFor(snapshot, nullptr);
    const QString ellipsis(QChar(0x2026));

    // The name is cut short - never the words round it, the suit led or the card.
    REQUIRE(rowOf(fields, 0).startsWith(QStringLiteral("Leader: Bartholomew")));
    REQUIRE(rowOf(fields, 0).endsWith(ellipsis + QStringLiteral(" · Led ♥")));
    REQUIRE(rowOf(fields, 1).startsWith(QStringLiteral("Winning: Bartholomew")));
    REQUIRE(rowOf(fields, 1).endsWith(ellipsis + QStringLiteral(", A♥")));
    REQUIRE(promptOf(fields).startsWith(QStringLiteral("Bartholomew")));
    REQUIRE(promptOf(fields).endsWith(ellipsis + QStringLiteral(" wins the trick.")));

    // A name that fits is left whole.
    REQUIRE(rowOf(trickpanel::fieldsFor(playing(), nullptr), 0) == QStringLiteral("Leader: Bogdan · Led ♥"));
}

TEST_CASE("The panel keeps your prompt through a redraw", "[trick-panel]")
{
    // A theme change redraws the table from the same snapshot while the engine is
    // still waiting on your card; the prompt is the panel's second source and only
    // MainWindow takes it back.
    TrickPanelItem panel(nullptr);

    GameSnapshot snapshot = playing();
    snapshot.activeSeat = 0;

    const auto shared = std::make_shared<const GameSnapshot>(snapshot);

    panel.setSnapshot(shared, Theme::dark());
    panel.setPrompt(promptOf({ HandEntry{ Card(Rank::Two, Suit::Hearts), true },
                               HandEntry{ Card(Rank::Ace, Suit::Spades), false } }));

    panel.setSnapshot(shared, Theme::light());

    REQUIRE(promptOf(panel.fields()) == QStringLiteral("Your turn. You must follow ♥."));

    panel.clearPrompt();

    REQUIRE(promptOf(panel.fields()) == QStringLiteral("Your turn."));
}
