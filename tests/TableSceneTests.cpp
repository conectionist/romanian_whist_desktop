#include <catch2/catch_test_macros.hpp>

#include "table/TableScene.h"
#include "table/HandItem.h"
#include "table/SeatItem.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSize>

#include <memory>

// THE HALF OF THE dpr CONTRACT THAT IS NOT THE PIXMAP'S.
//
// Theme::background() rasterizes at view * dpr and stamps the ratio on, and
// ThemeTests already holds that to account. What no test covered is the other
// end: drawBackground() handing that pixmap to a painter. A drawPixmap TARGET
// rect is logical and a SOURCE rect is not - Qt reads the source in the pixmap's
// own pixels - so a scene that passes the same rect as both samples the
// top-left 1/dpr of the background and stretches it over the whole viewport.
//
// That is invisible at dpr 1, which is where the suite and the developer's
// screen both are, and it is the reason this file renders at 2 rather than
// asserting anything about the picture itself.
namespace
{

// drawBackground() is protected, which is right - nothing outside the scene
// should be able to repaint the table. A using-declaration is the whole of what
// a test needs to reach it, and it needs no friend on the class under test.
struct ProbeScene : TableScene
{
    using TableScene::drawBackground;
};

const QSize view(320, 200);

constexpr qreal ratio = 2.0;

// A device-pixel-ratio-2 canvas the size the scene believes it is: 640x400
// physical pixels laid out over 320x200 logical ones, which is exactly what a
// viewport on a retina screen hands its scene.
QImage canvas()
{
    QImage image(view * ratio, QImage::Format_ARGB32);

    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    return image;
}

}

TEST_CASE("The table background is blitted 1:1 at a device pixel ratio of 2", "[table]")
{
    ProbeScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), view));
    scene.setTheme(Theme::light());

    QImage rendered = canvas();

    {
        QPainter painter(&rendered);
        scene.drawBackground(&painter, scene.sceneRect());
    }

    // The pixmap the scene should have drawn, asked for exactly as the scene
    // asks for it. Painted at 640x400, drawn over 320x200 logical pixels: one
    // pixmap pixel per canvas pixel, no scaling anywhere.
    const QImage expected =
        Theme::light().background(view, ratio).toImage().convertToFormat(QImage::Format_ARGB32);

    REQUIRE(rendered.size() == expected.size());

    // Before the fix this is the pixmap's top-left quarter blown up 2x: the
    // gradient runs at half speed and the vignette sits toward the bottom-right
    // corner rather than in the middle of the table.
    REQUIRE(rendered == expected);
}

// The size is only half of it. The source rect is also OFFSET by the exposed
// rect, so a partial repaint - which is what every repaint is once a card is
// moving over the table - reads from the wrong place besides being the wrong
// size. A fix that scaled the size and left the origin alone would pass the case
// above and fail every frame of a real game.
TEST_CASE("A partial repaint samples its own region of the background", "[table]")
{
    ProbeScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), view));
    scene.setTheme(Theme::light());

    const QRectF exposed(160.0, 0.0, 160.0, 200.0);

    QImage rendered = canvas();

    {
        QPainter painter(&rendered);
        scene.drawBackground(&painter, exposed);
    }

    const QImage expected =
        Theme::light().background(view, ratio).toImage().convertToFormat(QImage::Format_ARGB32);

    // The same region, in the physical pixels both images are addressed in.
    const QRect region(static_cast<int>(exposed.x() * ratio),
                       static_cast<int>(exposed.y() * ratio),
                       static_cast<int>(exposed.width() * ratio),
                       static_cast<int>(exposed.height() * ratio));

    REQUIRE(rendered.copy(region) == expected.copy(region));

    // And nothing outside it was touched, which is what the exposed rect is for.
    REQUIRE(rendered.pixelColor(0, 0).alpha() == 0);
}

// ---- the band the bid strip sits in, added in C2 ----
//
// SECTION 15.1's strip "rises over the bottom of the table", and the bottom of
// the table is where the HUMAN'S OWN PLAQUE is - seatlayout puts the seated
// player at bottom-centre. So "the bottom" and "where the strip goes" are
// different places, and getting that wrong is not subtle: C2's first run drew
// the strip straight through the player's own name.
//
// Geometry with a right answer, which is what this file is for.

namespace
{
GameSnapshot seatedTable(unsigned int seats, int humanSeat)
{
    GameSnapshot snapshot;

    snapshot.humanSeat = humanSeat;
    snapshot.roundNumber = 1;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.seats.resize(seats);

    for(unsigned int i = 0 ; i < seats ; i++)
        snapshot.seats[i].seatIndex = i;

    return snapshot;
}

// A window of the size a person actually plays in, rather than the 320x200 the
// dpr cases above use - a band is a question about a real table.
const QSize played(1000, 700);
}

TEST_CASE("The bottom band clears the seated player's own plaque", "[table]")
{
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));
    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    const QRectF band = scene.overlayBand();

    // NOT EMPTY, which is the assertion that actually failed first. An empty
    // band sends TableView to its fallback - the viewport's own bottom edge -
    // and the strip lands under the plaque instead of above it, which looks
    // like a placement bug rather than a band that was never computed.
    REQUIRE_FALSE(band.isEmpty());

    // ABOVE THE LOWEST PLAQUE, bounded independently rather than read back off
    // the scene. seatRingRect insets the scene by half a plaque, so the lowest
    // centre is at most sceneBottom - plaqueH/2 and its top edge at most
    // sceneBottom - plaqueH. A band reaching past that is one covering the
    // player's own name, which is the defect this case exists for.
    const qreal plaqueHeight =
        SeatItem::plaqueSize(Theme::light().cardHeightFor(played)).height();

    REQUIRE(band.bottom() <= played.height() - plaqueHeight);
    REQUIRE(band.top() < band.bottom());
}

TEST_CASE("The bottom band is empty before there is anything to place it against", "[table]")
{
    // No snapshot means no seats, so there is no plaque to clear and no band -
    // and the caller is handed an empty rect to fall back on rather than a
    // negative one to lay a widget out in.
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));

    REQUIRE(scene.overlayBand().isEmpty());
}

// ---- the strip reserved for the hand, added in C3 ----
//
// The bid strip could borrow the middle of the table because bidding finishes
// before the round's first card is played. A HAND CANNOT: cards are on the table
// while you are choosing one. And there was no room to borrow anyway - C2
// measured thirteen pixels between the trick ring and the bottom plaque - so the
// table gives the hand a strip and lays itself out in what is left.
//
// Section 14 is where this comes from ("a spectated game has no human... and the
// bottom hand area is not drawn at all") and SeatLayout.h has been describing the
// mechanism since B2: "no bottom strip is reserved for a hand, and that is the
// caller's business: it passes a shorter rect."

TEST_CASE("A seated table reserves a strip and lays out above it", "[table]")
{
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));
    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    const qreal cardHeight = Theme::light().cardHeightFor(played);

    const QRectF strip = scene.handRect(cardHeight);
    const QRectF table = scene.tableRect(cardHeight);

    REQUIRE_FALSE(strip.isEmpty());

    // AT LEAST A CARD TALL, which is the whole requirement: a fan drawn into a
    // strip shorter than a card is a fan with its top cut off.
    REQUIRE(strip.height() >= cardHeight);

    // The two partition the scene between them, with no gap and no overlap - a
    // gap is wasted green and an overlap is the collision this exists to stop.
    REQUIRE(table.bottom() <= strip.top() + 0.001);
    REQUIRE(table.bottom() >= strip.top() - 0.001);
    REQUIRE(strip.bottom() == played.height());
    REQUIRE(table.height() < played.height());
}

TEST_CASE("A spectated table reserves nothing", "[table]")
{
    // SECTION 14'S RULE, and the reason it is a rect rather than a flag: with
    // nobody seated there is no hand to draw, so the seats take the whole
    // ellipse and nothing downstream needs to know which kind of game it is.
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));
    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, -1)));

    const qreal cardHeight = Theme::light().cardHeightFor(played);

    REQUIRE(scene.handRect(cardHeight).isEmpty());
    REQUIRE(scene.tableRect(cardHeight) == scene.sceneRect());
}

TEST_CASE("The lowest plaque clears the reserved strip", "[table]")
{
    // THE COLLISION THE WHOLE CHANGE EXISTS TO PREVENT, and the direct descendant
    // of C2's band case: the seated player is drawn at bottom-centre, so without
    // a shorter rect their own plaque sits exactly where their hand goes.
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));
    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    const qreal cardHeight = Theme::light().cardHeightFor(played);

    const QRectF strip = scene.handRect(cardHeight);
    const qreal plaqueHeight = SeatItem::plaqueSize(cardHeight).height();

    // seatRingRect insets the TABLE rect by half a plaque, so the lowest centre
    // is at most tableRect.bottom() - plaqueH/2 and the plaque's own bottom edge
    // reaches tableRect.bottom() exactly. Bounded here from the public facts
    // rather than read back off the scene.
    const qreal lowestPlaqueBottom = scene.tableRect(cardHeight).bottom();

    REQUIRE(lowestPlaqueBottom <= strip.top() + 0.001);
    REQUIRE(plaqueHeight > 0.0);
}

TEST_CASE("The bid strip's band survives the reserved strip", "[table]")
{
    // C2'S GUARANTEE, re-asserted now that the plaques have moved. The band is
    // computed from the plaque positions, so reserving the hand strip moved it
    // without a line of overlayBand() changing - which is exactly the kind of
    // thing that is true until it is not.
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));
    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    const QRectF band = scene.overlayBand();
    const qreal cardHeight = Theme::light().cardHeightFor(played);

    REQUIRE_FALSE(band.isEmpty());

    // Above the hand as well as above the plaque, which is the new half: the
    // strip must not cover the cards you are bidding on.
    REQUIRE(band.bottom() <= scene.handRect(cardHeight).top());
}

TEST_CASE("A resize during a play prompt leaves the prompt's fan standing", "[table]")
{
    // THE TWO SOURCES MEETING AT THE WORST MOMENT. relayout() re-sources the fan
    // from the snapshot, and TableView calls it on every viewport resize - which
    // includes the one the View > Log toggle causes, at any moment the user likes,
    // while the engine is parked in playCard() waiting to be told which card.
    //
    // seatedTable()'s snapshot carries no hand, which is not a gap in the fixture:
    // that is exactly the shape a Forehead or Hidden round publishes (section 17),
    // and a fan re-sourced from it here empties out. Nothing left to click, no
    // digit key in range, and an answer the game can never be given.
    TableScene scene;

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), played));
    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    PlayPrompt prompt;

    prompt.promptId = 5;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    scene.handItem()->setPrompt(prompt);
    scene.handItem()->setPlayable(true);

    scene.setSceneRect(QRectF(QPointF(0.0, 0.0), QSizeF(played.width() * 0.7,
                                                        played.height() * 0.7)));
    scene.relayout();

    REQUIRE(scene.handItem()->slotCount() == 1);
    REQUIRE(scene.handItem()->isPlayable(0));
}
