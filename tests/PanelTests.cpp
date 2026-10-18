#include <catch2/catch_test_macros.hpp>

#include "paint/Panel.h"
#include "paint/Theme.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QSizeF>

#include <cmath>

// THE REDESIGN'S PANEL (section 2.2), painted once for every panel on the page:
// the seats from phase 4, the HUD and sheets after it. What has a right answer is
// where it paints, not the shades it paints in - those are checked by eye against
// the mock-up's screenshots.
namespace
{

const QSizeF size(280.0, 118.0);

// The picture's pixel at a point given in the PANEL's units, from its top-left.
QColor pixelAt(const panel::Picture& picture, QPointF point, qreal ratio)
{
    const QImage image = picture.pixmap.toImage();
    const QPointF inPicture = (point - picture.offset) * ratio;

    return image.pixelColor(QPoint(static_cast<int>(std::floor(inPicture.x())),
                                   static_cast<int>(std::floor(inPicture.y()))));
}

}

TEST_CASE("A panel's picture puts the panel on whole pixels", "[panel]")
{
    // A HAIRLINE BETWEEN TWO PIXELS IS TWO HALF-LINES. The panel's border and its
    // inset gold line are one unit each, so the picture's margin is grown to a
    // whole number of pixels at every ratio rather than left at the reach.
    for(const qreal ratio : { 0.5, 0.767, 1.0, 1.5, 2.0 })
    {
        INFO("ratio " << ratio);

        const panel::Picture picture = panel::render(size, Theme::dark().table, false, ratio);

        REQUIRE_FALSE(picture.pixmap.isNull());

        const qreal left = -picture.offset.x() * ratio;
        const qreal top = -picture.offset.y() * ratio;

        REQUIRE(std::abs(left - std::round(left)) < 1e-6);
        REQUIRE(std::abs(top - std::round(top)) < 1e-6);

        // AND IT HOLDS THE REACH, which the item's bounding rect promises.
        REQUIRE(-picture.offset.x() >= panel::reach().left());
        REQUIRE(-picture.offset.y() >= panel::reach().top());
    }
}

TEST_CASE("Only an active panel is ringed in gold", "[panel]")
{
    // The ring is a 3-unit band just outside the border, so its middle is 1.5
    // units out from the panel's left edge, halfway down.
    const Theme::TablePalette palette = Theme::dark().table;
    const QPointF onRing(-1.5, size.height() / 2.0);

    const QColor active = pixelAt(panel::render(size, palette, true, 1.0), onRing, 1.0);
    const QColor idle = pixelAt(panel::render(size, palette, false, 1.0), onRing, 1.0);

    REQUIRE(active.alpha() > 240);
    REQUIRE(std::abs(active.red() - palette.goldBright.red()) <= 2);
    REQUIRE(std::abs(active.green() - palette.goldBright.green()) <= 2);
    REQUIRE(std::abs(active.blue() - palette.goldBright.blue()) <= 2);

    // Idle, that pixel is only the drop shadow's edge: dark and translucent.
    REQUIRE(idle.alpha() < 160);
    REQUIRE(idle.lightness() < 40);
}

TEST_CASE("A panel's shadow falls below it", "[panel]")
{
    // 0 12 26: straight down, so more shadow under the panel than over it at the
    // same distance from its edge.
    const panel::Picture picture = panel::render(size, Theme::dark().table, false, 1.0);

    const QColor below = pixelAt(picture, QPointF(size.width() / 2.0, size.height() + 8.0), 1.0);
    const QColor above = pixelAt(picture, QPointF(size.width() / 2.0, -8.0), 1.0);

    REQUIRE(below.alpha() > above.alpha() + 40);
}
