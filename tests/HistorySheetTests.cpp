#include <catch2/catch_test_macros.hpp>

#include "paint/Theme.h"
#include "ui/HistorySheet.h"
#include "ui/NewGameDialog.h"
#include "WindowDriving.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QWidget>

#include <string>
#include <vector>

// The Round history (the table redesign's phase 11): the half with a right answer
// without a window, then the sheet.
namespace
{
RoundSeatResult seatResult(const std::string& name, unsigned int bet, unsigned int taken,
                           int roundScore, int totalScore)
{
    RoundSeatResult seat;
    seat.name = name;
    seat.hasBet = true;
    seat.bet = bet;
    seat.tricksWon = taken;
    seat.roundScore = roundScore;
    seat.totalScore = totalScore;
    return seat;
}

RoundResult roundOf(unsigned int number, unsigned int cards, std::vector<RoundSeatResult> seats)
{
    RoundResult result;
    result.roundNumber = number;
    result.roundCount = 26;
    result.trickCount = cards;
    result.seats = std::move(seats);
    return result;
}

// Three rounds of a three-seat game. Ana hits all three; Bogdan misses the second;
// Cristina misses the first and is paid a streak's ten in the third - which only
// the engine's own total can say.
std::vector<RoundResult> threeRounds()
{
    return {
        roundOf(1, 1, { seatResult("Ana", 0, 0, 5, 0),
                        seatResult("Bogdan", 1, 1, 6, 0),
                        seatResult("Cristina", 1, 0, -1, 0) }),
        roundOf(2, 1, { seatResult("Ana", 1, 1, 6, 5),
                        seatResult("Bogdan", 0, 1, -1, 6),
                        seatResult("Cristina", 0, 0, 5, -1) }),
        roundOf(3, 2, { seatResult("Ana", 0, 0, 5, 11),
                        seatResult("Bogdan", 1, 1, 6, 5),
                        seatResult("Cristina", 1, 1, 16, 4) })
    };
}

const std::vector<QString> threeNames = { QStringLiteral("Ana"), QStringLiteral("Bogdan"),
                                          QStringLiteral("Cristina") };

SheetTable* tableOf(const HistorySheet& sheet)
{
    SheetTable* rows = sheet.findChild<SheetTable*>("historyRows");
    REQUIRE(rows != nullptr);
    return rows;
}
}

TEST_CASE("A history cell's note is taken then the bid", "[history]")
{
    // THE OWNER'S CALL, 2026-10-22: the seat pill's order, "Taken/Bid", not the
    // mock-up's bid/taken. A slash reads as progress - one of two.
    REQUIRE(historysheet::noteFor(seatResult("Ana", 2, 1, -1, 0)) == QStringLiteral("1/2"));
    REQUIRE(historysheet::noteFor(seatResult("Ana", 0, 3, -3, 0)) == QStringLiteral("3/0"));

    // A seat that never bid has a dash where the bid goes, not a zero, which is a
    // bid.
    RoundSeatResult none = seatResult("Ana", 0, 0, 0, 0);
    none.hasBet = false;

    REQUIRE(historysheet::noteFor(none) == QStringLiteral("0/–"));
}

TEST_CASE("The history's totals are the engine's after the last round", "[history]")
{
    // The committed total plus the last round's worth - which carries Cristina's
    // streak bonus, so a sum of the rows (-1 + 5 + 16 = 20) would agree here only
    // by accident. Make the two disagree and the engine's must win.
    std::vector<RoundResult> rounds = threeRounds();
    rounds.back().seats[2].totalScore = 14;

    REQUIRE(historysheet::totalsFor(rounds, 3) == std::vector<int>{ 16, 11, 30 });

    // Before any round, nothing - for every seat.
    REQUIRE(historysheet::totalsFor({}, 4) == std::vector<int>{ 0, 0, 0, 0 });
}

TEST_CASE("The history has a signed and toned row per round then the totals", "[history]")
{
    HistorySheet sheet;
    sheet.showHistory(threeRounds(), threeNames);

    SheetTable* rows = tableOf(sheet);

    // Three rounds and the Total row.
    REQUIRE(rows->rowCount() == 4);

    // The mock-up's columns: Round, Cards, a player each, in seat order.
    REQUIRE(rows->heading(0)->text() == QStringLiteral("Round"));
    REQUIRE(rows->heading(1)->text() == QStringLiteral("Cards"));
    REQUIRE(rows->heading(2)->text() == QStringLiteral("Ana"));
    REQUIRE(rows->heading(4)->text() == QStringLiteral("Cristina"));

    // Round 2, of one card, and Bogdan's miss in it: minus one, in red, having
    // taken one trick on a bid of none.
    REQUIRE(rows->cell(1, 0)->text() == QStringLiteral("2"));
    REQUIRE(rows->cell(1, 1)->text() == QStringLiteral("1"));

    QLabel* miss = rows->cell(1, 3);
    REQUIRE(miss->text() == QStringLiteral("−1"));
    REQUIRE(rows->note(1, 3) == QStringLiteral("1/0"));
    REQUIRE(miss->palette().color(QPalette::WindowText) == Theme::dark().table.bad);

    // A hit: plus, green.
    QLabel* hit = rows->cell(1, 2);
    REQUIRE(hit->text() == QStringLiteral("+6"));
    REQUIRE(rows->note(1, 2) == QStringLiteral("1/1"));
    REQUIRE(hit->palette().color(QPalette::WindowText) == Theme::dark().table.good);

    // The streak's ten is in the round's worth, as the engine adds it.
    REQUIRE(rows->cell(2, 4)->text() == QStringLiteral("+16"));

    // The Total row, bold, with nothing under Cards.
    REQUIRE(rows->cell(3, 0)->text() == QStringLiteral("Total"));
    REQUIRE(rows->cell(3, 0)->font().bold());
    REQUIRE(rows->cell(3, 1)->text().isEmpty());
    REQUIRE(rows->cell(3, 2)->text() == QStringLiteral("16"));
    REQUIRE(rows->cell(3, 4)->text() == QStringLiteral("20"));
    REQUIRE(rows->cell(3, 4)->font().bold());

    // The line under the title says how to read a cell.
    QLabel* meta = sheet.findChild<QLabel*>("historyMeta");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->text().contains(QStringLiteral("taken/bid")));
}

TEST_CASE("A history with no round finished says so and heads its columns", "[history]")
{
    HistorySheet sheet;
    sheet.showHistory({}, threeNames);

    SheetTable* rows = tableOf(sheet);

    // Who is playing, a line saying there is nothing yet, and totals of nothing.
    REQUIRE(rows->heading(3)->text() == QStringLiteral("Bogdan"));
    REQUIRE(rows->rowCount() == 2);
    REQUIRE(rows->cell(0, 0)->text() == QStringLiteral("No rounds finished yet."));
    REQUIRE(rows->cell(1, 0)->text() == QStringLiteral("Total"));
    REQUIRE(rows->cell(1, 2)->text() == QStringLiteral("0"));
}

TEST_CASE("The history is rebuilt whole for a smaller game after a larger", "[history]")
{
    // The window keeps one sheet for every game it plays, and a three-seat game can
    // follow a six-seat one.
    HistorySheet sheet;

    std::vector<RoundResult> six = { roundOf(1, 1, {}) };
    std::vector<QString> sixNames;

    for(const char* name : { "Ana", "Bogdan", "Cristina", "Dan", "Elena", "Florin" })
    {
        six.back().seats.push_back(seatResult(name, 0, 0, 5, 0));
        sixNames.push_back(QString::fromUtf8(name));
    }

    sheet.showHistory(six, sixNames);
    REQUIRE(tableOf(sheet)->heading(7) != nullptr);

    sheet.showHistory(threeRounds(), threeNames);

    SheetTable* rows = tableOf(sheet);
    REQUIRE(rows->heading(4)->text() == QStringLiteral("Cristina"));
    REQUIRE(rows->heading(5) == nullptr);
    REQUIRE(rows->rowCount() == 4);
    REQUIRE(rows->cell(0, 5) == nullptr);

    int florins = 0;

    for(const QLabel* label : sheet.findChildren<QLabel*>())
    {
        if(label->text() == QStringLiteral("Florin"))
            florins++;
    }

    REQUIRE(florins == 0);
}

TEST_CASE("Six of the longest names fit the history's sheet", "[history]")
{
    // THE SHEET NEVER SCROLLS ACROSS, so a six-seat game of names as long as the
    // setup dialog takes gives way in its headings - and the table still fits the
    // full sheet's width inside its padding.
    const QString longest = QStringLiteral("Bartholomew Fairbank");
    REQUIRE(longest.size() == newgame::MaximumNameLength);

    std::vector<RoundResult> rounds = { roundOf(30, 1, {}) };
    std::vector<QString> names;

    for(int i = 0 ; i < 6 ; i++)
    {
        // The widest a cell gets: a miss of eight and a streak's ten.
        rounds.back().seats.push_back(seatResult(longest.toStdString(), 8, 0, -18, -100));
        names.push_back(longest);
    }

    QWidget page;
    page.resize(1920, 1034);

    HistorySheet sheet(&page);
    sheet.showHistory(rounds, names);
    page.show();
    QCoreApplication::processEvents();

    SheetTable* rows = tableOf(sheet);

    REQUIRE(rows->width() <= sheet::contentWidth);
    REQUIRE(rows->sizeHint().width() <= sheet::contentWidth);
    REQUIRE(rows->heading(2)->toolTip() == longest);
}

TEST_CASE("A long history opens at its foot with Close in view", "[history]")
{
    // A six-seat game deals thirty rounds and more, and about twenty-two rows fit
    // a 1080 window: the sheet opens on the latest round and the totals.
    std::vector<RoundResult> rounds;

    for(unsigned int r = 1 ; r <= 32 ; r++)
        rounds.push_back(roundOf(r, 1, threeRounds().front().seats));

    QWidget page;
    page.resize(1920, 1034);

    HistorySheet sheet(&page);
    sheet.hide();
    page.show();
    QCoreApplication::processEvents();

    sheet.showHistory(rounds, threeNames);
    sheet.show();
    sheet.raise();
    sheet.focusDefault();
    QCoreApplication::processEvents();

    QScrollBar* bar = sheet.findChild<QScrollArea*>()->verticalScrollBar();
    REQUIRE(bar->maximum() > 0);
    REQUIRE(bar->value() == bar->maximum());

    // focusWidget(), not hasFocus(), which the offscreen platform's never-active
    // windows answer false (MainWindowTests).
    QPushButton* close = sheet.findChild<QPushButton*>("closeHistory");
    REQUIRE(close != nullptr);
    REQUIRE(page.focusWidget() == close);
}

TEST_CASE("Six of the longest names fit a long history beside its scroll bar", "[history]")
{
    // PR #37's review, finding #3. A history that scrolls has its body narrowed by
    // the scroll bar, and the headings had been fitted to the whole 658: the table
    // ran under the bar, and the sheet, which never scrolls across, was cut off on
    // the right.
    const QString longest = QStringLiteral("Bartholomew Fairbank");
    REQUIRE(longest.size() == newgame::MaximumNameLength);

    std::vector<RoundSeatResult> seats;
    std::vector<QString> names;

    for(int i = 0 ; i < 6 ; i++)
    {
        seats.push_back(seatResult(longest.toStdString(), 8, 0, -18, -100));
        names.push_back(longest);
    }

    std::vector<RoundResult> rounds;

    for(unsigned int r = 1 ; r <= 32 ; r++)
        rounds.push_back(roundOf(r, 1, seats));

    QWidget page;
    page.resize(1920, 1034);

    HistorySheet sheet(&page);
    sheet.hide();
    page.show();
    QCoreApplication::processEvents();

    sheet.showHistory(rounds, names);
    sheet.show();
    sheet.raise();

    // A moment, not one pass: the bar's arrival on show refits the headings, and
    // the sheet takes a few passes to be laid out again round them.
    driving::pump(50);

    QScrollArea* scroll = sheet.findChild<QScrollArea*>();
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->verticalScrollBar()->isVisible());

    const int body = scroll->viewport()->width();
    SheetTable* rows = tableOf(sheet);

    REQUIRE(body < sheet::width);
    REQUIRE(scroll->widget()->minimumSizeHint().width() <= body);
    REQUIRE(scroll->widget()->width() == body);
    REQUIRE(rows->width() <= body - 2 * (sheet::border + sheet::paddingX));
    REQUIRE(rows->heading(2)->toolTip() == longest);
}

// AN OPEN HISTORY TAKES IN A ROUND SCORED UNDER IT (MainWindow::onRoundScored())
// and keeps the reader's place: at its foot, the new row and the totals come into
// view; scrolled up, nothing moves (PR #37's review, finding #4).
TEST_CASE("An open history keeps the reader's place as a round is added", "[history]")
{
    std::vector<RoundResult> rounds;

    for(unsigned int r = 1 ; r <= 32 ; r++)
        rounds.push_back(roundOf(r, 1, threeRounds().front().seats));

    QWidget page;
    page.resize(1920, 1034);

    HistorySheet sheet(&page);
    sheet.hide();
    page.show();
    QCoreApplication::processEvents();

    sheet.showHistory(rounds, threeNames);
    sheet.show();
    sheet.raise();
    sheet.focusDefault();
    driving::pump(50);

    QScrollBar* bar = sheet.findChild<QScrollArea*>()->verticalScrollBar();
    REQUIRE(bar->maximum() > 0);
    REQUIRE(bar->value() == bar->maximum());

    SECTION("At its foot, it follows the new row")
    {
        const int before = bar->maximum();

        rounds.push_back(roundOf(33, 1, threeRounds().front().seats));
        sheet.showHistory(rounds, threeNames);
        driving::pump(50);

        REQUIRE(bar->maximum() > before);
        REQUIRE(bar->value() == bar->maximum());
    }

    SECTION("Scrolled up, it stays where it is")
    {
        const int reading = bar->maximum() / 2;
        bar->setValue(reading);

        rounds.push_back(roundOf(33, 1, threeRounds().front().seats));
        sheet.showHistory(rounds, threeNames);
        driving::pump(50);

        REQUIRE(bar->value() == reading);
    }
}

TEST_CASE("The history's Close asks to close", "[history]")
{
    HistorySheet sheet;
    sheet.showHistory(threeRounds(), threeNames);

    int asked = 0;
    QObject::connect(&sheet, &HistorySheet::closeRequested, [&asked] { asked++; });

    QPushButton* close = sheet.findChild<QPushButton*>("closeHistory");
    REQUIRE(close != nullptr);
    REQUIRE(close->isDefault());

    close->click();
    REQUIRE(asked == 1);
}
