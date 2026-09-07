#include <catch2/catch_test_macros.hpp>

#include "table/TableScene.h"
#include "paint/Theme.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSize>

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
