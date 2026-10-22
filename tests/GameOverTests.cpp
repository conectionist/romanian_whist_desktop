#include <catch2/catch_test_macros.hpp>

#include "ui/GameOverPanel.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

using romanian_whist::Seat;
using romanian_whist::Standing;

// WHAT THE PANEL SAYS, and then what it shows - the same split ScoreboardTests
// and BidStripTests make, with one difference worth naming: the sentence half
// here is a PORT, so its cases are the terminal client's own, assertion for
// assertion.
//
// PORTED FROM romanian_whist_terminal, tests/RendererTests.cpp - the five cases
// that pin Renderer::winnerAnnouncement(). Section 21.4: the announcement moves
// to this panel "verbatim, plus tr()", and "its test comes with it". A wording
// that drifts between the two clients is exactly what section 9 is about.
namespace
{
// The panel reads only the name, score, seat and place, but a Standing is built
// whole here so a field added to it later fails to compile rather than silently
// defaulting. The terminal's helper, kept.
Standing standing(unsigned int seat, const std::string& name, int score, unsigned int place)
{
    return Standing{ Seat{seat}, name, score, place };
}

// The labels of one row of the panel's standings, left to right: place, name,
// score, star. Read back through the table rather than through
// findChildren<QLabel*>(), which would also return the title, the announcement and
// the headings - and would return them in construction order rather than in the
// order they are shown.
QStringList rowTexts(const GameOverPanel& panel, int row)
{
    SheetTable* rows = panel.findChild<SheetTable*>("standings");
    REQUIRE(rows);

    QStringList cells;

    for(int column = 0 ; column < 4 ; column++)
    {
        QLabel* label = rows->cell(row, column);
        REQUIRE(label);

        cells << label->text();
    }

    return cells;
}

int rowCount(const GameOverPanel& panel)
{
    SheetTable* rows = panel.findChild<SheetTable*>("standings");
    REQUIRE(rows);

    return rows->rowCount();
}
}

TEST_CASE("one winner is announced as a win", "[game-over]")
{
    const std::vector<Standing> winners{ standing(2, "Cristi", 47, 1) };

    REQUIRE(gameover::winnerAnnouncement(winners) == "Cristi wins with 47 points.");
}

TEST_CASE("a draw names every winner", "[game-over]")
{
    SECTION("two seats are joined with and, not a comma")
    {
        const std::vector<Standing> winners{ standing(0, "Ana", 47, 1),
                                             standing(1, "Bogdan", 47, 1) };

        REQUIRE(gameover::winnerAnnouncement(winners) ==
                "Ana and Bogdan tie for first with 47 points.");
    }

    SECTION("three seats take commas until the last, which takes the and")
    {
        const std::vector<Standing> winners{ standing(0, "Ana", 47, 1),
                                             standing(1, "Bogdan", 47, 1),
                                             standing(3, "Dan", 47, 1) };

        REQUIRE(gameover::winnerAnnouncement(winners) ==
                "Ana, Bogdan and Dan tie for first with 47 points.");
    }

    SECTION("a table that draws outright is still one sentence")
    {
        // Every seat level - the state the game starts in, and one a short game
        // can end in. Nothing here special-cases it, which is the point of
        // checking: the loop that joins the names has to hold at the top end
        // too.
        const std::vector<Standing> winners{ standing(0, "Ana", 0, 1),
                                             standing(1, "Bogdan", 0, 1),
                                             standing(2, "Cristi", 0, 1),
                                             standing(3, "Dan", 0, 1) };

        REQUIRE(gameover::winnerAnnouncement(winners) ==
                "Ana, Bogdan, Cristi and Dan tie for first with 0 points.");
    }
}

TEST_CASE("a negative winning score keeps its sign", "[game-over]")
{
    // A whole table can finish below zero - missed bids score negative and
    // nothing floors the total - so the winner's score is not always positive.
    const std::vector<Standing> winners{ standing(1, "Bogdan", -12, 1) };

    REQUIRE(gameover::winnerAnnouncement(winners) == "Bogdan wins with -12 points.");
}

TEST_CASE("no winners is no announcement", "[game-over]")
{
    // getWinners() is never empty for a started game, so this is the defensive
    // branch rather than a state the app reaches. It earns its place by saying
    // what happens instead of leaving a bare "wins with" line to be discovered.
    REQUIRE(gameover::winnerAnnouncement({}).isEmpty());
}

// ---- the panel itself, offscreen ----

TEST_CASE("The panel ranks by the engine's places, not by row", "[game-over]")
{
    // SECTION 16.2's OWN TABLE: 47, 47, 31, 28 reads 1, 1, 3, 4. A row counter
    // would number it 1, 2, 3, 4 and quietly rank two seats the engine ranked
    // equal - which is the whole reason Standing carries a place at all.
    const std::vector<Standing> standings{ standing(0, "You", 47, 1),
                                           standing(1, "Ana", 47, 1),
                                           standing(2, "Bogdan", 31, 3),
                                           standing(3, "Carmen", 28, 4) };

    const std::vector<Standing> winners{ standings[0], standings[1] };

    GameOverPanel panel;
    panel.showResult(standings, winners);

    REQUIRE(rowCount(panel) == 4);

    REQUIRE(rowTexts(panel, 0) == QStringList{ "1.", "You", "47", QString::fromUtf8("★") });
    REQUIRE(rowTexts(panel, 1) == QStringList{ "1.", "Ana", "47", QString::fromUtf8("★") });

    // The row after a two-way tie for first is third, and neither of the two
    // below the tie is starred.
    REQUIRE(rowTexts(panel, 2) == QStringList{ "3.", "Bogdan", "31", "" });
    REQUIRE(rowTexts(panel, 3) == QStringList{ "4.", "Carmen", "28", "" });
}

TEST_CASE("Every winner is starred, from getWinners()", "[game-over]")
{
    // THE STAR FOLLOWS THE PAYLOAD AND NOT THE PLACE, and this is the case that
    // tells the two apart: the engine has ranked one seat first, so a panel that
    // starred `place == 1` would agree here by luck. Handed a winners list of
    // one, it must star that seat and no other - which is what it will be handed
    // when the two ever disagree.
    const std::vector<Standing> standings{ standing(0, "Ana", 47, 1),
                                           standing(1, "Bogdan", 31, 2) };

    GameOverPanel panel;
    panel.showResult(standings, { standings[1] });

    REQUIRE(rowTexts(panel, 0).at(3) == "");
    REQUIRE(rowTexts(panel, 1).at(3) == QString::fromUtf8("★"));
}

TEST_CASE("A negative standing takes the sheets' minus sign", "[game-over]")
{
    // PR #36's review, finding #6: the round sheet's figures write U+2212, as wide
    // as a digit in tabular figures, and the standings are the same kind of table.
    const std::vector<Standing> standings{ standing(0, "Ana", 7, 1),
                                           standing(1, "Bogdan", -12, 2) };

    GameOverPanel panel;
    panel.showResult(standings, { standings[0] });

    REQUIRE(rowTexts(panel, 0).at(2) == QStringLiteral("7"));
    REQUIRE(rowTexts(panel, 1).at(2) == QStringLiteral("\u221212"));
}

TEST_CASE("A second game over replaces the first one's table", "[game-over]")
{
    // The panel outlives every game in it - it is a child of the stack, built
    // once - so the rows of game one must not still be there under game two's.
    // QGridLayout is what makes this worth pinning: rowCount() never shrinks,
    // so a panel that asked the layout where to write would start below them.
    const std::vector<Standing> first{ standing(0, "Ana", 47, 1),
                                       standing(1, "Bogdan", 31, 2),
                                       standing(2, "Cristi", 12, 3) };

    const std::vector<Standing> second{ standing(0, "Dan", 8, 1),
                                        standing(1, "Elena", 3, 2) };

    GameOverPanel panel;
    panel.showResult(first, { first[0] });
    panel.showResult(second, { second[0] });

    REQUIRE(rowCount(panel) == 2);
    REQUIRE(rowTexts(panel, 0) == QStringList{ "1.", "Dan", "8", QString::fromUtf8("★") });
}

TEST_CASE("A winner named in markup is announced as typed", "[game-over]")
{
    // PR #36's review, finding #5: the names come from the setup dialog as the
    // player typed them, and the announcement once read "<b>Dan</b>" as bold. So
    // does a failure's message, which is whatever an exception said.
    const std::vector<Standing> standings{ standing(0, "<b>Dan</b>", 63, 1),
                                           standing(1, "Ana", 40, 2) };

    GameOverPanel panel;
    panel.showResult(standings, { standings[0] });

    QLabel* announcement = panel.findChild<QLabel*>("announcement");
    REQUIRE(announcement);
    REQUIRE(announcement->textFormat() == Qt::PlainText);
    REQUIRE(announcement->text().startsWith(QStringLiteral("<b>Dan</b>")));

    panel.showFailure(QStringLiteral("<i>bad</i> state"));
    REQUIRE(announcement->textFormat() == Qt::PlainText);
}

TEST_CASE("The failure face shows no standings and no Play again", "[game-over]")
{
    // THE OTHER ENDING, and the reason the panel has two faces: the run slot
    // caught something, so there is no final table to draw and nothing to repeat
    // - but there still has to be a way off this screen, which is the defect
    // this whole panel was written to fix.
    GameOverPanel panel;
    panel.showResult({ standing(0, "Ana", 47, 1) }, { standing(0, "Ana", 47, 1) });
    panel.showFailure("the deck ran out");

    REQUIRE(rowCount(panel) == 0);

    QPushButton* playAgain = panel.findChild<QPushButton*>("playAgain");
    QPushButton* mainMenu = panel.findChild<QPushButton*>("gameOverMainMenu");

    REQUIRE(playAgain);
    REQUIRE(mainMenu);

    // isVisible() is false for every widget on a panel that was never shown, so
    // the question has to be the one that is answerable here: isHidden() is the
    // explicit hide, independent of whether an ancestor is up.
    REQUIRE(playAgain->isHidden());
    REQUIRE_FALSE(mainMenu->isHidden());

    QLabel* announcement = panel.findChild<QLabel*>("announcement");
    REQUIRE(announcement);
    REQUIRE(announcement->text() == "the deck ran out");
}

TEST_CASE("The standings offer the round history between their two ways on", "[game-over]")
{
    // The owner's call, 2026-10-22 (phase 11): the standings' dim covers the
    // table's own Round history button, so the panel carries one - quiet, between
    // Main menu and Play again, and on the failure face as well, since a game that
    // failed still played the rounds it scored.
    GameOverPanel panel;
    panel.showResult({ standing(0, "Ana", 47, 1) }, { standing(0, "Ana", 47, 1) });

    QPushButton* history = panel.findChild<QPushButton*>("gameOverHistory");
    QPushButton* mainMenu = panel.findChild<QPushButton*>("gameOverMainMenu");
    QPushButton* playAgain = panel.findChild<QPushButton*>("playAgain");
    REQUIRE(history != nullptr);
    REQUIRE(mainMenu != nullptr);
    REQUIRE(playAgain != nullptr);

    REQUIRE(static_cast<SheetButton*>(history)->kind() == SheetButton::Kind::Quiet);
    REQUIRE(playAgain->isDefault());
    REQUIRE_FALSE(history->autoDefault());

    panel.resize(1280, 754);
    panel.show();
    QCoreApplication::processEvents();

    REQUIRE(mainMenu->geometry().right() < history->geometry().left());
    REQUIRE(history->geometry().right() < playAgain->geometry().left());

    int asked = 0;
    QObject::connect(&panel, &GameOverPanel::historyRequested, [&asked] { asked++; });

    history->click();
    REQUIRE(asked == 1);

    panel.showFailure("the deck ran out");
    REQUIRE_FALSE(history->isHidden());

    // THE GREEN ONE STILL LAST: Main menu is the green one on this face, so it
    // goes after Round history, and Tab follows (PR #37's review, finding #1).
    QCoreApplication::processEvents();

    REQUIRE(static_cast<SheetButton*>(mainMenu)->kind() == SheetButton::Kind::Primary);
    REQUIRE(history->geometry().right() < mainMenu->geometry().left());
    REQUIRE(history->nextInFocusChain() == mainMenu);

    // And back again for a game that ends as games do, on the same panel.
    panel.showResult({ standing(0, "Ana", 47, 1) }, { standing(0, "Ana", 47, 1) });
    QCoreApplication::processEvents();

    REQUIRE(mainMenu->geometry().right() < history->geometry().left());
    REQUIRE(history->geometry().right() < playAgain->geometry().left());
    REQUIRE(mainMenu->nextInFocusChain() == history);
    REQUIRE(history->nextInFocusChain() == playAgain);
}
