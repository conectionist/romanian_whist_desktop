#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "TableFixtures.h"

#include "paint/CardRenderer.h"
#include "paint/Theme.h"
#include "ui/LastTrickSheet.h"
#include "ui/Overlay.h"

#include <romanian_whist/Card.h>

#include <QCoreApplication>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <cmath>
#include <optional>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// The Last trick (the table redesign's phase 11): what the window keeps of the
// table, then the sheet that shows it.
namespace
{
// fullTable()'s five cards and Seat 0's, the trick finished: a card from every
// seat, led by Seat 1, whose King of Spades is still winning when you play.
GameSnapshot finishedTrick()
{
    GameSnapshot snapshot = fullTable();
    snapshot.table.push_back(PlayedCardView{ 0, Card(Rank::Two, Suit::Spades) });
    return snapshot;
}

// The next trick under way: Seat 1, who took the last, leading again.
GameSnapshot nextTrickStarted()
{
    GameSnapshot snapshot = fullTable();
    snapshot.trickNumber = 4;
    snapshot.table = { PlayedCardView{ 1, Card(Rank::Ace, Suit::Hearts), true } };
    return snapshot;
}

std::vector<unsigned int> seatsOf(const lasttrick::Trick& trick)
{
    std::vector<unsigned int> seats;

    for(const lasttrick::Played& played : trick.cards)
        seats.push_back(played.seatIndex);

    return seats;
}
}

TEST_CASE("A trick is kept only once every seat has played to it", "[last-trick]")
{
    lasttrick::Tracker tracker;

    // Five cards of six down.
    tracker.observe(fullTable());
    REQUIRE_FALSE(tracker.last().has_value());

    // The sixth.
    tracker.observe(finishedTrick());
    REQUIRE(tracker.last().has_value());

    const lasttrick::Trick& kept = *tracker.last();

    // In play order, the leader's first - not seat order.
    REQUIRE(seatsOf(kept) == std::vector<unsigned int>{ 1, 2, 3, 4, 5, 0 });
    REQUIRE(kept.cards.front().card == Card(Rank::King, Suit::Spades));
    REQUIRE(kept.cards.front().name == QStringLiteral("Seat 1"));
    REQUIRE(kept.roundNumber == 10);
    REQUIRE(kept.humanSeat == 0);

    // The winner is the engine's, as the frame carries it.
    REQUIRE(kept.cards.front().isWinning);
    REQUIRE_FALSE(kept.cards.back().isWinning);
}

TEST_CASE("The kept trick outlasts the next one's first cards", "[last-trick]")
{
    lasttrick::Tracker tracker;
    tracker.observe(finishedTrick());

    tracker.observe(nextTrickStarted());

    REQUIRE(tracker.last().has_value());
    REQUIRE(tracker.last()->cards.size() == 6);
    REQUIRE(tracker.last()->cards.front().card == Card(Rank::King, Suit::Spades));

    // And an empty table between tricks leaves it be too.
    GameSnapshot between = fullTable();
    between.table.clear();
    tracker.observe(between);

    REQUIRE(tracker.last().has_value());
}

TEST_CASE("The last trick outlasts its round", "[last-trick]")
{
    // THE OWNER'S CALL, 2026-10-22: "Last trick" is the last trick played, so at the
    // start of a round it is the last round's final trick. The mock-up forgets it
    // when it deals, and phase 11 first did too.
    lasttrick::Tracker tracker;
    tracker.observe(finishedTrick());

    // The next round's first frame: dealt, nothing played.
    GameSnapshot dealt = fullTable();
    dealt.roundNumber = 11;
    dealt.trickNumber = 0;
    dealt.table.clear();

    tracker.observe(dealt);
    REQUIRE(tracker.last().has_value());
    REQUIRE(tracker.last()->roundNumber == 10);
    REQUIRE(tracker.last()->cards.front().card == Card(Rank::King, Suit::Spades));

    // The new round's first trick finished takes its place.
    GameSnapshot first = finishedTrick();
    first.roundNumber = 11;
    first.table.front().card = Card(Rank::Ace, Suit::Spades);

    tracker.observe(first);
    REQUIRE(tracker.last().has_value());
    REQUIRE(tracker.last()->roundNumber == 11);
    REQUIRE(tracker.last()->cards.front().card == Card(Rank::Ace, Suit::Spades));

    // And only clear() forgets it, as a new game does.
    tracker.clear();
    REQUIRE_FALSE(tracker.last().has_value());
}

TEST_CASE("The last trick says who won it and You for you", "[last-trick]")
{
    lasttrick::Trick trick;
    trick.roundNumber = 3;
    trick.humanSeat = 0;
    trick.cards = { { Card(Rank::Ten, Suit::Hearts), 2, QStringLiteral("Ana"), false },
                    { Card(Rank::King, Suit::Hearts), 0, QStringLiteral("Dan"), true },
                    { Card(Rank::Two, Suit::Hearts), 1, QStringLiteral("Bogdan"), false } };

    REQUIRE(lasttrick::metaFor(trick, 3) == QStringLiteral("You won it"));

    trick.cards[1].isWinning = false;
    trick.cards[0].isWinning = true;

    REQUIRE(lasttrick::metaFor(trick, 3) == QStringLiteral("Ana won it"));

    // A game you watch has no You: your seat's number is the bots', by name.
    trick.humanSeat = -1;
    trick.cards[0].isWinning = false;
    trick.cards[1].isWinning = true;

    REQUIRE(lasttrick::metaFor(trick, 3) == QStringLiteral("Dan won it"));

    // And with nothing finished in the game yet, the mock-up's sentence, less its
    // "this round".
    REQUIRE(lasttrick::metaFor(std::nullopt, 1) == QStringLiteral("No trick has finished yet."));
}

TEST_CASE("A trick from an earlier round says which round it was", "[last-trick]")
{
    // At the start of round 4 the last trick is round 3's, and its cards alone do
    // not say so.
    lasttrick::Trick trick;
    trick.roundNumber = 3;
    trick.humanSeat = 0;
    trick.cards = { { Card(Rank::Ten, Suit::Hearts), 2, QStringLiteral("Ana"), true },
                    { Card(Rank::Two, Suit::Hearts), 0, QStringLiteral("Dan"), false } };

    REQUIRE(lasttrick::metaFor(trick, 4) == QStringLiteral("Ana won it \u00B7 round 3"));

    trick.cards[0].isWinning = false;
    trick.cards[1].isWinning = true;

    REQUIRE(lasttrick::metaFor(trick, 4) == QStringLiteral("You won it \u00B7 round 3"));

    // A name with a placeholder in it is not filled in by the round.
    trick.humanSeat = -1;
    trick.cards[1].name = QStringLiteral("Dan %2");

    REQUIRE(lasttrick::metaFor(trick, 4) == QStringLiteral("Dan %2 won it \u00B7 round 3"));
}

TEST_CASE("The last trick shows each card over its player in play order", "[last-trick]")
{
    CardRenderer cards;

    QWidget page;
    page.resize(1920, 1034);

    LastTrickSheet sheet(&page);
    sheet.setCards(&cards);
    sheet.setTheme(Theme::dark());

    lasttrick::Tracker tracker;
    tracker.observe(finishedTrick());
    sheet.showTrick(tracker.last(), 10);

    page.show();
    QCoreApplication::processEvents();

    REQUIRE(sheet.shownTrick().has_value());
    REQUIRE(seatsOf(*sheet.shownTrick()) == std::vector<unsigned int>{ 1, 2, 3, 4, 5, 0 });

    QLabel* meta = sheet.findChild<QLabel*>("lastTrickMeta");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->text() == QStringLiteral("Seat 1 won it"));

    // .mini's row: six cards 88 wide, 12 apart, the names under them.
    const QSizeF card = lasttrick::cardSize();
    REQUIRE(card.width() == 88.0);
    REQUIRE(std::abs(card.height() - 127.8) < 0.5);

    const QRect area = sheet.cardsArea();
    REQUIRE(area.width() == 6 * 88 + 5 * 12);
    REQUIRE(area.height() > std::ceil(card.height()) + lasttrick::nameAbove);

    // Drawn: the winner, first, ringed in gold-bright just outside its card's
    // left edge; the second card, which lost, has no ring there.
    const QImage image = sheet.grab().toImage();
    const QPoint body = sheet.findChild<QWidget*>("lastTrickCards")->mapTo(&sheet, QPoint());
    const QColor gold = Theme::dark().table.goldBright;

    const auto goldAt = [&](qreal x)
    {
        const QColor pixel = image.pixelColor(QPoint(static_cast<int>(body.x() + x),
                                                     static_cast<int>(body.y() + card.height() / 2.0)));
        return std::abs(pixel.red() - gold.red()) <= 8 && std::abs(pixel.green() - gold.green()) <= 8
            && std::abs(pixel.blue() - gold.blue()) <= 8;
    };

    REQUIRE(goldAt(-1.0));
    REQUIRE_FALSE(goldAt(lasttrick::cardRect(1, card).left() - 1.0));
}

// THE ART'S SHAPE FROM THE ONE PLACE THAT KNOWS IT, with a renderer or without:
// the sheet without one kept a copy of the art's numbers (PR #37's review, finding
// #7), which a change of art would have left behind.
TEST_CASE("Without its art the last trick keeps the art's card shape", "[last-trick]")
{
    CardRenderer cards;

    lasttrick::Tracker tracker;
    tracker.observe(finishedTrick());

    QWidget page;
    page.resize(1920, 1034);

    LastTrickSheet drawn(&page);
    drawn.setCards(&cards);
    drawn.showTrick(tracker.last(), 10);

    LastTrickSheet bare(&page);
    bare.showTrick(tracker.last(), 10);

    page.show();
    QCoreApplication::processEvents();

    REQUIRE_FALSE(drawn.cardsArea().isEmpty());
    REQUIRE(bare.cardsArea().size() == drawn.cardsArea().size());
}

namespace
{
// A trick of `count` cards, the first one winning.
lasttrick::Trick trickOf(std::size_t count)
{
    static const char* names[] = { "Ana", "Bogdan", "Cristina", "Dan", "Elena", "Florin" };

    lasttrick::Trick trick;
    trick.roundNumber = 5;
    trick.humanSeat = -1;

    for(std::size_t i = 0 ; i < count ; i++)
        trick.cards.push_back({ Card(Rank::Ace, Suit::Spades), static_cast<unsigned int>(i),
                                QString::fromLatin1(names[i]), i == 0 });

    return trick;
}

// How far `widget`'s middle is from the middle of the sheet, across.
int offCentre(const LastTrickSheet& sheet, const QWidget* widget)
{
    const QPoint middle = widget->mapTo(&sheet, widget->rect().center());
    return std::abs(middle.x() - sheet.sheetRect().center().x());
}
}

// THE OWNER'S CALL, 2026-10-23: the title, the line under it, the cards and Close
// down the middle, where the mock-up has them on the left and Close on the right.
TEST_CASE("The last trick's title cards and Close are centred", "[last-trick]")
{
    CardRenderer cards;

    QWidget page;
    page.resize(1920, 1034);

    LastTrickSheet sheet(&page);
    sheet.setCards(&cards);
    sheet.showTrick(trickOf(3), 5);

    page.show();
    QCoreApplication::processEvents();

    QLabel* title = sheet.findChild<QLabel*>("lastTrickTitle");
    QLabel* meta = sheet.findChild<QLabel*>("lastTrickMeta");
    QPushButton* close = sheet.findChild<QPushButton*>("closeLastTrick");

    REQUIRE(title != nullptr);
    REQUIRE(meta != nullptr);
    REQUIRE(close != nullptr);

    // The words centred in their lines, and the lines across the sheet.
    REQUIRE(title->alignment().testFlag(Qt::AlignHCenter));
    REQUIRE(meta->alignment().testFlag(Qt::AlignHCenter));
    REQUIRE(offCentre(sheet, title) <= 1);
    REQUIRE(offCentre(sheet, meta) <= 1);

    // The row of cards and Close in the middle.
    REQUIRE(offCentre(sheet, sheet.findChild<QWidget*>("lastTrickCards")) <= 1);
    REQUIRE(offCentre(sheet, close) <= 1);
}

// AS WIDE AS IT NEEDS: its row of cards and the padding round it, so the sheet
// narrows with the number of players - but never narrower than its words or
// Close, which would cut them short.
TEST_CASE("The last trick is as wide as its cards need", "[last-trick]")
{
    CardRenderer cards;

    QWidget page;
    page.resize(1920, 1034);
    page.show();

    const auto widthFor = [&](const std::optional<lasttrick::Trick>& trick)
    {
        LastTrickSheet sheet(&page);
        sheet.setCards(&cards);
        sheet.showTrick(trick, 5);
        sheet.show();
        QCoreApplication::processEvents();

        // Whatever the width, every word shows whole.
        for(const char* name : { "lastTrickTitle", "lastTrickMeta" })
        {
            const QLabel* label = sheet.findChild<QLabel*>(name);

            INFO(name);
            REQUIRE(label->width() >= label->sizeHint().width());
        }

        const QPushButton* close = sheet.findChild<QPushButton*>("closeLastTrick");
        REQUIRE(close->width() >= close->sizeHint().width());

        return sheet.sheetRect().width();
    };

    constexpr int padding = 2 * (sheet::border + sheet::paddingX);

    // Six cards: the row, 6 x 88 and 5 gaps of 12, inside the padding.
    REQUIRE(widthFor(trickOf(6)) == 6 * 88 + 5 * 12 + padding);
    REQUIRE(widthFor(trickOf(6)) < sheet::width);

    // Fewer players, a narrower sheet, down to three.
    REQUIRE(widthFor(trickOf(4)) == 4 * 88 + 3 * 12 + padding);
    REQUIRE(widthFor(trickOf(3)) == 3 * 88 + 2 * 12 + padding);

    // Two cards are narrower than a long line under them, which then sets the
    // width: the longest name the setup dialog takes, in a trick from the round
    // before.
    lasttrick::Trick longName = trickOf(2);
    longName.roundNumber = 4;
    longName.cards.front().name = QStringLiteral("Bartholomew Fairbank");

    REQUIRE(widthFor(trickOf(2)) == 2 * 88 + 12 + padding);
    REQUIRE(widthFor(longName) > widthFor(trickOf(2)));
    REQUIRE(widthFor(longName) < sheet::width);

    // And with none finished, the line that says so.
    REQUIRE(widthFor(std::nullopt) < sheet::width);
}

TEST_CASE("The last trick with none finished shows no cards", "[last-trick]")
{
    LastTrickSheet sheet;
    sheet.showTrick(std::nullopt, 1);

    REQUIRE_FALSE(sheet.shownTrick().has_value());
    REQUIRE(sheet.cardsArea().isEmpty());

    QLabel* meta = sheet.findChild<QLabel*>("lastTrickMeta");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->text() == QStringLiteral("No trick has finished yet."));
}

TEST_CASE("The last trick's Close asks to close", "[last-trick]")
{
    LastTrickSheet sheet;

    int asked = 0;
    QObject::connect(&sheet, &LastTrickSheet::closeRequested, [&asked] { asked++; });

    QPushButton* close = sheet.findChild<QPushButton*>("closeLastTrick");
    REQUIRE(close != nullptr);
    REQUIRE(close->isDefault());

    close->click();
    REQUIRE(asked == 1);
}
