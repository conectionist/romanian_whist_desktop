#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CardStringMaker.h"

#include "paint/CardRenderer.h"
#include "paint/Theme.h"
#include "table/RackItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"

#include <QGraphicsScene>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

#include <cmath>
#include <numbers>

using Catch::Approx;
using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// THE RACKS (the table redesign's section 2.6): each opponent's hand standing on the
// rim, one back per card left, and the Forehead card face up in it. The geometry is
// free functions, checked against the mock-up's numbers; the item is checked for
// what it chooses to show, which is where a leak of a hidden card would be.
namespace
{

constexpr qreal degrees = std::numbers::pi / 180.0;

SeatView holding(unsigned int cardsLeft)
{
    SeatView seat;
    seat.seatIndex = 1;
    seat.cardsLeft = cardsLeft;

    return seat;
}

SeatView showing(Card card)
{
    SeatView seat = holding(1);
    seat.hand.push_back(HandEntry{ card, true });

    return seat;
}

// The rect a seat's rack keeps clear of: its panel, grown by 10 (table.js covered()).
QRectF clearOf(const QRectF& panel)
{
    return panel.adjusted(-10.0, -10.0, 10.0, 10.0);
}

}

// ---- where a rack stands ----

TEST_CASE("Every rack stands on the rim clear of its own panel", "[rack]")
{
    for(unsigned int count = 2 ; count <= 6 ; count++)
    {
        for(int humanSeat : { 0, -1 })
        {
            for(unsigned int seat = 0 ; seat < count ; seat++)
            {
                INFO(count << " seats, human " << humanSeat << ", seat " << seat);

                const QRectF panel = seatlayout::panelRect(count, humanSeat, seat);
                const QPointF origin = seatlayout::rackOrigin(seatlayout::rackAngle(count, humanSeat, seat));

                REQUIRE(QLineF(tablespace::centre, origin).length() == Approx(tablespace::rackRadius));
                REQUIRE_FALSE(clearOf(panel).contains(origin));
            }
        }
    }
}

TEST_CASE("Four players' racks face their panels squarely", "[rack]")
{
    // Left, top and right: each panel's centre is straight out from the table's,
    // and with section 2.5's panels no rack has to slide.
    REQUIRE(seatlayout::rackAngle(4, 0, 1) == Approx(std::numbers::pi));
    REQUIRE(seatlayout::rackAngle(4, 0, 2) == Approx(-std::numbers::pi / 2.0));
    REQUIRE(seatlayout::rackAngle(4, 0, 3) == Approx(0.0).margin(1e-12));
}

TEST_CASE("The bottom seat's rack is at the bottom", "[rack]")
{
    // A spectated game's seat 0 sits where you would, and stands its rack on the rim
    // straight below the centre.
    REQUIRE(seatlayout::rackAngle(4, -1, 0) == Approx(std::numbers::pi / 2.0));
    REQUIRE(seatlayout::rackOrigin(std::numbers::pi / 2.0).x() == Approx(tablespace::centre.x()));
}

TEST_CASE("A rack under its own panel slides clockwise until it is clear", "[rack]")
{
    // THE RULE NO PANEL OF SECTION 2.5 NEEDS, so a panel made up to need it: the top
    // seat's, brought down over the rim. The rack slides a degree at a time,
    // clockwise - an increasing angle, since y is down - until it is clear, and then
    // 9 degrees more for the width of its row.
    const QRectF panel(628.0, 0.0, 280.0, 118.0);
    const qreal straight = -std::numbers::pi / 2.0;

    REQUIRE(clearOf(panel).contains(seatlayout::rackOrigin(straight)));

    const qreal angle = seatlayout::rackAngle(panel);

    REQUIRE(angle > straight);
    REQUIRE_FALSE(clearOf(panel).contains(seatlayout::rackOrigin(angle)));

    // The last degree before the 9 was still covered, or it would have stopped there.
    REQUIRE(clearOf(panel).contains(seatlayout::rackOrigin(angle - 10.0 * degrees)));
    REQUIRE_FALSE(clearOf(panel).contains(seatlayout::rackOrigin(angle - 9.0 * degrees)));
}

TEST_CASE("A rack's frame points out toward its owner", "[rack]")
{
    // The rack's local +y is the direction from the table's centre through the rack,
    // so its cards lean back toward the seat they belong to.
    for(int step = 0 ; step < 24 ; step++)
    {
        const qreal angle = step * 15.0 * degrees;

        QTransform frame;
        frame.rotate(rackitem::frameRotation(angle));

        const QPointF out = frame.map(QPointF(0.0, 1.0));

        INFO("angle " << step * 15);
        REQUIRE(out.x() == Approx(std::cos(angle)).margin(1e-9));
        REQUIRE(out.y() == Approx(std::sin(angle)).margin(1e-9));
    }
}

TEST_CASE("Quarter-turn racks are turned exactly", "[rack]")
{
    // Exactly, so QTransform::rotate() takes its exact path and the racks of four
    // lie square on the pixel grid.
    REQUIRE(rackitem::frameRotation(0.0) == -90.0);
    REQUIRE(rackitem::frameRotation(std::numbers::pi / 2.0) == 0.0);
    REQUIRE(rackitem::frameRotation(std::numbers::pi) == 90.0);
    REQUIRE(rackitem::frameRotation(-std::numbers::pi / 2.0) == 180.0);

    // And as rackAngle() hands them over.
    REQUIRE(rackitem::frameRotation(seatlayout::rackAngle(4, 0, 1)) == 90.0);
    REQUIRE(rackitem::frameRotation(seatlayout::rackAngle(4, 0, 2)) == 180.0);
    REQUIRE(rackitem::frameRotation(seatlayout::rackAngle(4, 0, 3)) == -90.0);
    REQUIRE(rackitem::frameRotation(seatlayout::rackAngle(4, -1, 0)) == 0.0);
}

// ---- the cards in it ----

TEST_CASE("A rack's card is the mock-up's size and the art's shape", "[rack]")
{
    const CardRenderer renderer;

    REQUIRE(rackitem::cardSize() == QSizeF(62.0, 90.0));
    REQUIRE(renderer.sizeForHeight(tablespace::rackCardHeight) == rackitem::cardSize().toSize());
}

TEST_CASE("A tilted card is wider at its far end than at the rail", "[rack]")
{
    for(std::size_t count = 1 ; count <= rackitem::mostCards ; count++)
    {
        for(std::size_t i = 0 ; i < count ; i++)
        {
            INFO(count << " cards, card " << i);

            const QPolygonF quad = rackitem::cardQuad(count, i);
            REQUIRE(quad.size() == 4);

            const qreal near = QLineF(quad[0], quad[1]).length();
            const qreal far = QLineF(quad[2], quad[3]).length();

            REQUIRE(far > near);

            // And the far end is further out, toward the owner.
            REQUIRE((quad[2].y() + quad[3].y()) / 2.0 > (quad[0].y() + quad[1].y()) / 2.0);
        }
    }
}

TEST_CASE("The middle card projects to the mock-up's trapezoid", "[rack]")
{
    // A lone card has no turn and no shift, so the perspective alone: its near edge
    // at y 4 is 62 wide, and its far edge, raised 90 sin 50 toward a camera 300 away,
    // is magnified 300 / 231.06.
    const QPolygonF quad = rackitem::cardQuad(1, 0);
    const qreal k = 300.0 / (300.0 - 90.0 * std::sin(50.0 * degrees));

    REQUIRE(quad[0].y() == Approx(4.0));
    REQUIRE(QLineF(quad[0], quad[1]).length() == Approx(62.0));
    REQUIRE(QLineF(quad[3], quad[2]).length() == Approx(62.0 * k));
    REQUIRE(quad[2].y() == Approx(40.0 + (4.0 + 90.0 * std::cos(50.0 * degrees) - 40.0) * k));
    REQUIRE(rackitem::farMagnification() == Approx(k));
}

TEST_CASE("A rack's row is symmetric about its middle", "[rack]")
{
    for(std::size_t count = 1 ; count <= rackitem::mostCards ; count++)
    {
        for(std::size_t i = 0 ; i < count ; i++)
        {
            INFO(count << " cards, card " << i);

            const QPolygonF quad = rackitem::cardQuad(count, i);
            const QPolygonF mirror = rackitem::cardQuad(count, count - 1 - i);

            // Mirrored in x, left and right corners swapped.
            for(int corner : { 0, 1, 2, 3 })
            {
                const int opposite = corner ^ 1;
                REQUIRE(quad[corner].x() == Approx(-mirror[opposite].x()).margin(1e-9));
                REQUIRE(quad[corner].y() == Approx(mirror[opposite].y()).margin(1e-9));
            }
        }
    }
}

TEST_CASE("The rail covers every card's edge at the rack", "[rack]")
{
    // The middle of each near edge. The outer cards' corners are turned up to 7
    // degrees and poke out past the rail's ends, as they do in the mock-up.
    for(std::size_t count = 1 ; count <= rackitem::mostCards ; count++)
    {
        const QRectF rail = rackitem::railRect(count);

        for(std::size_t i = 0 ; i < count ; i++)
        {
            INFO(count << " cards, card " << i);

            const QPolygonF quad = rackitem::cardQuad(count, i);
            REQUIRE(rail.contains((quad[0] + quad[1]) / 2.0));
        }
    }

    REQUIRE(rackitem::railRect(0).isEmpty());
}

TEST_CASE("The card transform maps the flat card onto its quad", "[rack]")
{
    const QRectF flat(QPointF(), rackitem::cardSize());
    const QTransform transform = rackitem::cardTransform(5, 1);
    const QPolygonF quad = rackitem::cardQuad(5, 1);

    const QPointF corners[] = { flat.topLeft(), flat.topRight(), flat.bottomRight(), flat.bottomLeft() };

    for(int i = 0 ; i < 4 ; i++)
    {
        INFO("corner " << i);
        REQUIRE(transform.map(corners[i]).x() == Approx(quad[i].x()).margin(1e-6));
        REQUIRE(transform.map(corners[i]).y() == Approx(quad[i].y()).margin(1e-6));
    }

    // Projective, not affine: that is the tilt.
    REQUIRE_FALSE(transform.isAffine());
}

// ---- what a rack shows ----

TEST_CASE("A rack holds one back per card left", "[rack]")
{
    for(unsigned int left = 0 ; left <= 8 ; left++)
    {
        const rackitem::Shown shown = rackitem::shownFor(holding(left));

        INFO(left << " cards left");
        REQUIRE(shown.count == left);
        REQUIRE(shown.faces.empty());
    }
}

TEST_CASE("A rack shows faces only when the seat's hand can be seen", "[rack]")
{
    // BOTH DIRECTIONS, so a rack that always drew faces fails too. A non-empty hand
    // IS the permission: the snapshot fills it only where canSeeHand() allows.
    const Card card(Rank::King, Suit::Spades);

    const rackitem::Shown seen = rackitem::shownFor(showing(card));
    REQUIRE(seen.count == 1);
    REQUIRE(seen.faces == std::vector<Card>{ card });

    const rackitem::Shown unseen = rackitem::shownFor(holding(1));
    REQUIRE(unseen.count == 1);
    REQUIRE(unseen.faces.empty());
}

TEST_CASE("An empty rack draws nothing at all", "[rack]")
{
    // No cards and no rail, as table.js draws neither for an empty hand.
    const CardRenderer renderer;
    RackItem rack(&renderer, nullptr);

    rack.setRack(holding(0), Theme::dark());

    REQUIRE(rack.picture(1.0).isNull());

    rack.setRack(holding(3), Theme::dark());

    REQUIRE_FALSE(rack.picture(1.0).isNull());
}

TEST_CASE("A rack's rect does not change as its cards go", "[rack]")
{
    // Sized for a full rack, so a card leaving re-indexes nothing.
    const CardRenderer renderer;
    RackItem rack(&renderer, nullptr);

    rack.setRack(holding(8), Theme::dark());
    const QRectF full = rack.boundingRect();

    rack.setRack(holding(1), Theme::dark());
    REQUIRE(rack.boundingRect() == full);

    // And it holds everything a full rack draws.
    REQUIRE(full.contains(rackitem::extent(8)));
}

TEST_CASE("A rack is composed at the ratio it is drawn at", "[rack]")
{
    // B1'S CONTRACT, which every pixmap on the table keeps: made at the pixels it
    // lands on, at half scale and into a ratio-2 image alike.
    const CardRenderer renderer;

    QGraphicsScene scene;
    auto* rack = new RackItem(&renderer, nullptr);
    rack->setRack(holding(5), Theme::dark());
    scene.addItem(rack);

    for(const auto& [scale, deviceRatio] : { std::pair{ 0.5, 1.0 }, std::pair{ 1.0, 2.0 } })
    {
        INFO("scale " << scale << ", device ratio " << deviceRatio);

        QImage image(QSize(400, 300) * deviceRatio, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(deviceRatio);
        image.fill(Qt::transparent);

        const QRectF bounds = rack->boundingRect();
        const std::size_t before = rack->compositions();

        QPainter painter(&image);
        scene.render(&painter, QRectF(0.0, 0.0, bounds.width() * scale, bounds.height() * scale), bounds);
        painter.end();

        // Each pass is a ratio the rack has not been drawn at, so it composed once.
        REQUIRE(rack->compositions() == before + 1);

        // AT THAT RATIO AND NO OTHER: the picture at it is the one already held. Had
        // paint() asked for another ratio, this would compose afresh.
        const std::size_t drawn = rack->compositions();

        REQUIRE(rack->picture(scale * deviceRatio).devicePixelRatio() == Approx(scale * deviceRatio));
        REQUIRE(rack->compositions() == drawn);
    }
}

TEST_CASE("A turned rack is drawn on whole pixels", "[rack]")
{
    // The left rack of four, turned a quarter turn, at a rim point that is not on
    // a whole pixel: it is drawn exactly as the same rack at the nearest whole
    // pixel, rather than filtered between pixels.
    const CardRenderer renderer;

    const auto drawnAt = [&](QPointF at)
    {
        QGraphicsScene scene;
        auto* rack = new RackItem(&renderer, nullptr);
        rack->setRack(holding(5), Theme::dark());
        rack->setRotation(rackitem::frameRotation(std::numbers::pi));
        rack->setPos(at);
        scene.addItem(rack);

        const QRectF all(0.0, 0.0, 400.0, 300.0);

        QImage image(all.size().toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        scene.render(&painter, all, all);
        painter.end();

        return image;
    };

    REQUIRE(drawnAt(QPointF(200.3, 150.6)) == drawnAt(QPointF(200.0, 151.0)));
}

TEST_CASE("A snapshot that changes nothing in a rack composes nothing", "[rack]")
{
    const CardRenderer renderer;
    RackItem rack(&renderer, nullptr);

    rack.setRack(holding(4), Theme::dark());
    rack.picture(1.0);

    const std::size_t composed = rack.compositions();

    // Another snapshot of the same seat: its score and bid change, its cards do not.
    SeatView later = holding(4);
    later.totalScore = 12;
    later.hasBet = true;
    later.bet = 2;

    rack.setRack(later, Theme::dark());
    rack.picture(1.0);

    REQUIRE(rack.compositions() == composed);

    // A card played is a new picture.
    rack.setRack(holding(3), Theme::dark());
    rack.picture(1.0);

    REQUIRE(rack.compositions() == composed + 1);
}

TEST_CASE("A rack takes no click", "[rack]")
{
    const CardRenderer renderer;
    RackItem rack(&renderer, nullptr);

    REQUIRE(rack.acceptedMouseButtons() == Qt::NoButton);
}

TEST_CASE("Racks showing the same cards share one picture", "[rack]")
{
    // A rack is drawn in its own frame and turned by its item, so racks holding the
    // same cards are one picture: five opponents on a settle compose it once.
    const CardRenderer renderer;
    rackitem::Cache racks;

    RackItem left(&renderer, &racks);
    RackItem right(&renderer, &racks);

    left.setRack(holding(5), Theme::dark());
    right.setRack(holding(5), Theme::dark());

    left.picture(1.0);
    right.picture(1.0);

    REQUIRE(racks.renders() == 1);
    REQUIRE(left.picture(1.0).cacheKey() == right.picture(1.0).cacheKey());

    // A rack one card down is another picture.
    right.setRack(holding(4), Theme::dark());
    right.picture(1.0);

    REQUIRE(racks.renders() == 2);
}
