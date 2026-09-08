#include <catch2/catch_test_macros.hpp>

#include "table/TableScene.h"
#include "table/SeatItem.h"
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
