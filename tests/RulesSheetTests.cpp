#include <catch2/catch_test_macros.hpp>

#include "ui/NewGameDialog.h"
#include "ui/RulesSheet.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/Scoreboard.h>

#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTextDocumentFragment>

// The menu bar's Rules (the redesign's phase 9): the mock-up's showRules(), and the
// engine's docs/RULES.md behind it, for the game the client actually deals.
namespace
{
using romanian_whist::GameStructure;
using romanian_whist::Rank;

// A seated game at `seats`, otherwise the dialog's defaults.
newgame::Config tableOf(unsigned int seats)
{
    newgame::Config config = newgame::defaults();

    config.spectator = false;
    config.botNames.clear();
    config.botLevels.clear();

    for(unsigned int i = 1 ; i < seats ; i++)
    {
        config.botNames << QStringLiteral("Bot %1").arg(i);
        config.botLevels.push_back(newgame::BotLevel::Random);
    }

    return config;
}

bool anyLineHas(const QStringList& lines, const QString& text)
{
    for(const QString& line : lines)
        if(line.contains(text))
            return true;

    return false;
}

}

TEST_CASE("The deck is two ranks per player down from the Ace", "[rules]")
{
    // RULES.md rule 2's table.
    REQUIRE(rulessheet::lowestRank(2) == Rank::Jack);
    REQUIRE(rulessheet::lowestRank(3) == Rank::Nine);
    REQUIRE(rulessheet::lowestRank(4) == Rank::Seven);
    REQUIRE(rulessheet::lowestRank(5) == Rank::Five);
    REQUIRE(rulessheet::lowestRank(6) == Rank::Three);

    REQUIRE(rulessheet::rankName(Rank::Jack) == QStringLiteral("Jack"));
    REQUIRE(rulessheet::rankName(Rank::Nine) == QStringLiteral("9"));
    REQUIRE(rulessheet::rankName(Rank::Three) == QStringLiteral("3"));
}

TEST_CASE("The rules name the deck for every table size", "[rules]")
{
    const struct
    {
        unsigned int seats;
        const char* expected;
    } tables[] = {
        { 2, "2 players use Ace down to Jack: eight cards each, 16 in all." },
        { 3, "3 players use Ace down to 9: eight cards each, 24 in all." },
        { 4, "4 players use Ace down to 7: eight cards each, 32 in all." },
        { 5, "5 players use Ace down to 5: eight cards each, 40 in all." },
        { 6, "6 players use Ace down to 3: eight cards each, 48 in all." },
    };

    for(const auto& table : tables)
    {
        CAPTURE(table.seats);
        REQUIRE(rulessheet::linesFor(tableOf(table.seats)).front() ==
                QString::fromUtf8(table.expected));
    }

    // A watched game counts the bots alone: nobody sits in for you.
    newgame::Config watched = tableOf(4);
    watched.spectator = true;
    REQUIRE(rulessheet::linesFor(watched).front().startsWith(QStringLiteral("3 players")));
}

TEST_CASE("The rules give the schedule in the game's own order", "[rules]")
{
    newgame::Config config = tableOf(4);
    config.endWithForeheadAndHidden = false;

    config.structure = GameStructure::S_181;
    REQUIRE(rulessheet::linesFor(config).at(1) ==
            QString::fromUtf8("Rounds go 1 (×4), 2, 3 … 7, 8 (×4), 7 … 3, 2, 1 (×4): 24 rounds."));

    config.structure = GameStructure::S_818;
    REQUIRE(rulessheet::linesFor(config).at(1) ==
            QString::fromUtf8("Rounds go 8 (×4), 7 … 3, 2, 1 (×4), 2, 3 … 7, 8 (×4): 24 rounds."));

    // The appended pair, and the count newgame::roundCount() gives, which is the
    // dialog's own.
    config.endWithForeheadAndHidden = true;
    REQUIRE(newgame::roundCount(config) == 26u);
    REQUIRE(rulessheet::linesFor(config).at(1) ==
            QString::fromUtf8("Rounds go 8 (×4), 7 … 3, 2, 1 (×4), 2, 3 … 7, 8 (×4), then a "
                              "Forehead and a Hidden round: 26 rounds."));
}

TEST_CASE("The rules name the special rounds only when the game deals them", "[rules]")
{
    newgame::Config config = tableOf(4);

    // Neither: not a word about either.
    config.endWithForeheadAndHidden = false;
    config.all1GamesAreForehead = false;

    QStringList lines = rulessheet::linesFor(config);
    REQUIRE_FALSE(anyLineHas(lines, QStringLiteral("Forehead")));
    REQUIRE_FALSE(anyLineHas(lines, QStringLiteral("Hidden")));

    // The appended pair: both explained.
    config.endWithForeheadAndHidden = true;

    lines = rulessheet::linesFor(config);
    REQUIRE(lines.contains(QStringLiteral(
        "In a Forehead round you see everyone's card but your own while you bid.")));
    REQUIRE(lines.contains(QStringLiteral(
        "In a Hidden round nobody sees their card until the bidding is done.")));
    REQUIRE_FALSE(lines.contains(QStringLiteral("Every 1-card round is a Forehead round.")));

    // Every one-card round Forehead, and no Hidden round at all.
    config.endWithForeheadAndHidden = false;
    config.all1GamesAreForehead = true;

    lines = rulessheet::linesFor(config);
    REQUIRE(lines.contains(QStringLiteral("Every 1-card round is a Forehead round.")));
    REQUIRE(lines.contains(QStringLiteral(
        "In a Forehead round you see everyone's card but your own while you bid.")));
    REQUIRE_FALSE(anyLineHas(lines, QStringLiteral("Hidden")));

    // BOTH, and the appended Hidden round is a 1-card round that is NOT
    // Forehead: the engine keeps its type (Scoreboard's "all1GamesAreForehead"
    // case). The sheet said every 1-card round was, and in the app the last
    // round of such a game was Hidden (PR #35's review, finding #5).
    config.endWithForeheadAndHidden = true;
    config.all1GamesAreForehead = true;

    lines = rulessheet::linesFor(config);
    REQUIRE(lines.contains(QStringLiteral(
        "Every 1-card round is a Forehead round, except the Hidden round at the end.")));
    REQUIRE_FALSE(lines.contains(QStringLiteral("Every 1-card round is a Forehead round.")));
    REQUIRE(lines.contains(QStringLiteral(
        "In a Hidden round nobody sees their card until the bidding is done.")));
}

TEST_CASE("The rules end on trump bidding play and scoring", "[rules]")
{
    const QStringList lines = rulessheet::linesFor(tableOf(4));

    REQUIRE(lines.size() >= 4);

    const QStringList last = lines.mid(lines.size() - 4);

    REQUIRE(last.at(0).startsWith(QStringLiteral("Below 8 cards, the next card is turned as trump.")));
    REQUIRE(last.at(1).contains(QStringLiteral("The last bidder may not make the bids add up")));
    REQUIRE(last.at(2).contains(QStringLiteral("You never have to overtrump.")));

    // RULES.md rule 8's exception, which the mock-up's sheet leaves out.
    REQUIRE(last.at(3).contains(QStringLiteral("1-card rounds neither add to a run nor break one")));
}

TEST_CASE("The rules sheet shows every rule and its Close asks to close", "[rules]")
{
    RulesSheet sheet;
    sheet.resize(1000, 700);

    const newgame::Config config = tableOf(5);
    sheet.showRules(config);

    QLabel* text = sheet.findChild<QLabel*>(QStringLiteral("rulesText"));
    REQUIRE(text != nullptr);

    // Escaped on the way in, so the label's plain reading holds each line whole.
    const QString shown = QTextDocumentFragment::fromHtml(text->text()).toPlainText();

    for(const QString& line : rulessheet::linesFor(config))
    {
        CAPTURE(line);
        REQUIRE(shown.contains(line));
    }

    QPushButton* closeButton = sheet.findChild<QPushButton*>(QStringLiteral("rulesClose"));
    REQUIRE(closeButton != nullptr);

    // A lambda rather than QSignalSpy, which lives in Qt6::Test.
    int closed = 0;
    QObject::connect(&sheet, &RulesSheet::closeRequested, [&closed]{ closed++; });

    closeButton->click();
    REQUIRE(closed == 1);
}
