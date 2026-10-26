#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/GameSnapshot.h"
#include "paint/Theme.h"
#include "table/BidBubbleItem.h"
#include "table/Motion.h"
#include "table/SeatLayout.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"

#include <QVariantAnimation>

#include <memory>

using Catch::Approx;

// A BID, SAID OUT LOUD (the table redesign's phase 12): page.html's .bubble over the
// seat that has just bid. Its English is safe to assert on, as the strip's is: no
// translator is installed in the tests.
namespace
{

// A table of `seats` in round `round`, you at seat 0, with the seats in `bid` bid.
GameSnapshot bidding(unsigned int seats, std::vector<unsigned int> bid, unsigned int round = 1, int human = 0)
{
    GameSnapshot snapshot;
    snapshot.humanSeat = human;
    snapshot.roundNumber = round;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.phase = romanian_whist::GamePhase::Betting;
    snapshot.seats.resize(seats);

    for(unsigned int i = 0 ; i < seats ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].isHuman = static_cast<int>(i) == human;
    }

    for(unsigned int seat : bid)
    {
        snapshot.seats[seat].hasBet = true;
        snapshot.seats[seat].bet = seat + 1;
    }

    snapshot.betsPlaced = static_cast<unsigned int>(bid.size());
    return snapshot;
}

void show(TableScene& scene, const GameSnapshot& snapshot)
{
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));
}

}

TEST_CASE("A bubble says the bid, and whose", "[bubble]")
{
    REQUIRE(bidbubble::textFor(false, 2) == QStringLiteral("Bids 2"));
    REQUIRE(bidbubble::textFor(true, 0) == QStringLiteral("You bid 0"));
}

// page.html's @keyframes bubble: in and up over the first 12%, held to 80%, out.
TEST_CASE("A bubble rises in, holds and fades", "[bubble]")
{
    const bidbubble::Frame start = bidbubble::frameAt(0.0);
    REQUIRE(start.opacity == Approx(0.0).margin(1e-6));
    REQUIRE(start.rise == Approx(6.0));

    const bidbubble::Frame in = bidbubble::frameAt(0.12);
    REQUIRE(in.opacity == Approx(1.0));
    REQUIRE(in.rise == Approx(0.0).margin(1e-6));

    for(const qreal held : { 0.2, 0.5, 0.8 })
    {
        INFO("at " << held);
        REQUIRE(bidbubble::frameAt(held).opacity == 1.0);
        REQUIRE(bidbubble::frameAt(held).rise == 0.0);
    }

    REQUIRE(bidbubble::frameAt(0.9).opacity > 0.0);
    REQUIRE(bidbubble::frameAt(0.9).opacity < 1.0);
    REQUIRE(bidbubble::frameAt(1.0).opacity == Approx(0.0).margin(1e-6));
}

// Centred on the panel, its top 18 above the panel's padding box: `top: -18px` in a
// panel with a 1-unit border.
TEST_CASE("A bubble sits centred over the top of its panel", "[bubble]")
{
    const QRectF panel(100.0, 200.0, 259.0, 118.0);
    const QRectF bubble = bidbubble::rectOver(panel, QStringLiteral("Bids 3"));

    REQUIRE(bubble.center().x() == Approx(panel.center().x()));
    REQUIRE(bubble.top() == Approx(panel.top() + 1.0 - 18.0));
    REQUIRE(bubble.bottom() > panel.top());
    REQUIRE(bubble.width() > 28.0);
}

TEST_CASE("A bot's bid and yours are said over their panels", "[bubble]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setMotion(motion::Pace::Normal);

    show(scene, bidding(4, {}));

    for(std::size_t i = 0 ; i < 4 ; i++)
        REQUIRE_FALSE(scene.bidBubble(i)->isVisible());

    show(scene, bidding(4, { 1 }));

    BidBubbleItem* bot = scene.bidBubble(1);
    REQUIRE(bot->isVisible());
    REQUIRE(bot->text() == QStringLiteral("Bids 2"));
    REQUIRE(bot->zValue() == tablespace::z::bubbles);
    REQUIRE(bot->pos() == bidbubble::rectOver(seatlayout::panelRect(4, 0, 1), bot->text()).topLeft());

    // The others say nothing.
    REQUIRE_FALSE(scene.bidBubble(0)->isVisible());
    REQUIRE_FALSE(scene.bidBubble(2)->isVisible());

    // The same frame again says nothing new: the bubble already up runs on.
    bot->clock()->pause();
    bot->clock()->setCurrentTime(500);
    show(scene, bidding(4, { 1 }));
    REQUIRE(bot->clock()->currentTime() == 500);

    show(scene, bidding(4, { 1, 0 }));
    REQUIRE(scene.bidBubble(0)->isVisible());
    REQUIRE(scene.bidBubble(0)->text() == QStringLiteral("You bid 1"));
}

TEST_CASE("A bubble goes when its time is up", "[bubble]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());

    SECTION("1.6 s with motion")
    {
        scene.setMotion(motion::Pace::Normal);
        show(scene, bidding(4, {}));
        show(scene, bidding(4, { 2 }));

        BidBubbleItem* bubble = scene.bidBubble(2);
        bubble->clock()->pause();
        REQUIRE(bubble->clock()->duration() == motion::bubbleMs);

        // Fading in at first, from below its place.
        REQUIRE(bubble->opacity() < 0.01);
        REQUIRE(bubble->transform().dy() == Approx(6.0));

        bubble->clock()->setCurrentTime(800);
        REQUIRE(bubble->opacity() == 1.0);
        REQUIRE(bubble->isVisible());

        bubble->clock()->setCurrentTime(motion::bubbleMs);
        REQUIRE_FALSE(bubble->isVisible());
    }

    SECTION("1.5 s held still at Off")
    {
        show(scene, bidding(4, {}));
        show(scene, bidding(4, { 2 }));

        BidBubbleItem* bubble = scene.bidBubble(2);
        bubble->clock()->pause();
        REQUIRE(bubble->clock()->duration() == motion::bubbleStillMs);

        // Still: all there from the first moment, at its place.
        REQUIRE(bubble->opacity() == 1.0);
        REQUIRE(bubble->transform().dy() == 0.0);

        bubble->clock()->setCurrentTime(1400);
        REQUIRE(bubble->opacity() == 1.0);

        bubble->clock()->setCurrentTime(motion::bubbleStillMs);
        REQUIRE_FALSE(bubble->isVisible());
    }
}

TEST_CASE("A spectated game's bids are all said as a bot's", "[bubble]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());

    show(scene, bidding(4, {}, 1, -1));
    show(scene, bidding(4, { 0 }, 1, -1));

    REQUIRE(scene.bidBubble(0)->isVisible());
    REQUIRE(scene.bidBubble(0)->text() == QStringLiteral("Bids 1"));
}

TEST_CASE("A new game and a new round say nothing of the last", "[bubble]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setMotion(motion::Pace::Normal);

    show(scene, bidding(4, {}, 7));
    show(scene, bidding(4, { 3 }, 7));
    REQUIRE(scene.bidBubble(3)->isVisible());

    SECTION("the next round's frame, its bids already in, says nothing")
    {
        show(scene, bidding(4, { 1, 2 }, 8));
        REQUIRE_FALSE(scene.bidBubble(1)->isVisible());
        REQUIRE_FALSE(scene.bidBubble(2)->isVisible());
    }

    SECTION("a new game clears what was being said")
    {
        scene.newGame();
        REQUIRE_FALSE(scene.bidBubble(3)->isVisible());

        show(scene, bidding(4, {}, 1));
        REQUIRE_FALSE(scene.bidBubble(3)->isVisible());
    }

    // PR #38's review, finding #2: the round number cannot tell this game from the
    // last, so newGame() is what does.
    SECTION("a new game in the same round clears it too, and says its own bids")
    {
        scene.newGame();
        show(scene, bidding(4, {}, 7));
        REQUIRE_FALSE(scene.bidBubble(3)->isVisible());

        show(scene, bidding(4, { 1 }, 7));
        REQUIRE(scene.bidBubble(1)->isVisible());
        REQUIRE(scene.bidBubble(1)->text() == QStringLiteral("Bids 2"));
        REQUIRE_FALSE(scene.bidBubble(3)->isVisible());
    }

    SECTION("a table of another size clears it too")
    {
        show(scene, bidding(3, {}, 7));
        REQUIRE_FALSE(scene.bidBubble(3)->isVisible());
    }
}
