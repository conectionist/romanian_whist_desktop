#include <catch2/catch_test_macros.hpp>

#include "table/TableScene.h"
#include "table/TableView.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

// HOW LONG A FRAME OF A WINDOW DRAG TAKES - a tool, not a test, for the table
// redesign's phase 3. The tag is hidden, so neither a bare run nor ctest picks it
// up and the suite's count does not move. Run it on the release preset, since a
// debug build's times say little about the shipped one:
//
//     ./build-release/tests/desktop_tests "[.timing]"
//
// It drags a full table's window edge in small steps, painting each step at once
// as a screen refresh would, and prints what the frames cost: the drag's first
// step, which TableView draws sharp; the rest of it, settling as TableView does
// it; and - for comparison - every step settled at once, which is what each frame
// cost before phase 3. Phase 3's target is a drag frame under about 8 ms.
//
// NOT AN ASSERTION, because a time is a fact about the machine it ran on: a
// threshold here would fail on a slow runner and prove nothing on a fast one.
// TableViewTests asserts what the mechanism does - nothing rasterized while a
// resize settles - and this measures what that is worth.
namespace
{
using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// Six plaques, eight cards in your hand and five on the felt, court cards among
// both: the court cards' art is the slow kind to rasterize.
GameSnapshot fullTable()
{
    GameSnapshot snapshot;

    snapshot.humanSeat = 0;
    snapshot.roundNumber = 10;
    snapshot.roundCount = 32;
    snapshot.trickCount = 8;
    snapshot.seats.resize(6);

    for(unsigned int i = 0 ; i < 6 ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].name = "Seat " + std::to_string(i);
        snapshot.seats[i].cardsLeft = 8;
    }

    snapshot.seats[0].isHuman = true;

    snapshot.hand = { HandEntry{ Card(Rank::Two, Suit::Clubs) },
                      HandEntry{ Card(Rank::Seven, Suit::Clubs) },
                      HandEntry{ Card(Rank::Jack, Suit::Clubs) },
                      HandEntry{ Card(Rank::Nine, Suit::Diamonds) },
                      HandEntry{ Card(Rank::Queen, Suit::Diamonds) },
                      HandEntry{ Card(Rank::Ten, Suit::Hearts) },
                      HandEntry{ Card(Rank::King, Suit::Hearts) },
                      HandEntry{ Card(Rank::Ace, Suit::Spades) } };

    snapshot.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Spades) },
                       PlayedCardView{ 2, Card(Rank::Queen, Suit::Spades) },
                       PlayedCardView{ 3, Card(Rank::Jack, Suit::Spades) },
                       PlayedCardView{ 4, Card(Rank::Eight, Suit::Spades) },
                       PlayedCardView{ 5, Card(Rank::Three, Suit::Spades) } };

    return snapshot;
}

constexpr int steps = 40;

struct Times
{
    double median = 0.0;
    double maximum = 0.0;
};

Times summarise(std::vector<double> times)
{
    std::sort(times.begin(), times.end());
    return Times{ times[times.size() / 2], times.back() };
}

// One step of the drag: the window a few pixels bigger, and the frame painted now.
double step(TableView& view, int at, bool settleEachStep)
{
    QElapsedTimer timer;
    timer.start();

    view.resize(1600 + 3 * at, 900 + 2 * at);

    if(settleEachStep)
        view.finishResize();

    view.viewport()->repaint();

    return static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
}

}

TEST_CASE("Frame times while the window's edge is dragged", "[.timing]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));

    TableView view(&scene);
    view.resize(1600, 900);
    view.show();
    QCoreApplication::processEvents();

    view.finishResize();
    view.viewport()->repaint();

    // THE DRAG AS TableView DRAWS IT: the first step sharp, at full cost, which is
    // what keeps a one-off resize from ever being soft - and every step after it
    // frozen, the pixmaps scaled.
    const double first = step(view, 1, false);

    std::vector<double> settling;

    for(int at = 2 ; at <= steps ; at++)
        settling.push_back(step(view, at, false));

    // AND THE FRAME AFTER IT, where everything is rasterized once at the new scale.
    QElapsedTimer timer;
    timer.start();
    view.finishResize();
    view.viewport()->repaint();
    const double settled = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;

    // THE SAME DRAG SETTLED AT EVERY STEP, which is what a frame cost before.
    std::vector<double> eager;

    for(int at = steps + 1 ; at <= 2 * steps ; at++)
        eager.push_back(step(view, at, true));

    const Times drag = summarise(settling);
    const Times before = summarise(eager);

    std::printf("drag, first step:      %6.2f ms, drawn sharp\n", first);
    std::printf("drag, settling:        median %6.2f ms, max %6.2f ms over %d steps\n",
                drag.median, drag.maximum, steps - 1);
    std::printf("the frame it settles:  %6.2f ms\n", settled);
    std::printf("drag, settled per step: median %6.2f ms, max %6.2f ms over %d steps\n",
                before.median, before.maximum, steps);

    SUCCEED();
}
