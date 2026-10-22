#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "paint/DevicePixels.h"

#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QWidget>

#include <cmath>
#include <optional>

// THE ONE RULE FOR HOW MANY PIXELS A PIXMAP NEEDS, and the one quantization every
// pixmap cache keys that number on. Both have a right answer that does not need a
// scene or a widget to check.

namespace
{

// A canvas at a given ratio. Its size does not matter to the ratio; it only has to
// be something a painter can be opened on.
QImage canvas(qreal ratio)
{
    QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);

    image.setDevicePixelRatio(ratio);

    return image;
}

// A widget that asks effectiveRatio() from its own paint event, which is the only
// place a painter can be opened on a widget.
class RatioProbe : public QWidget
{
public:
    std::optional<qreal> seen;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        seen = devicepixels::effectiveRatio(painter);
    }
};

}

TEST_CASE("The effective ratio is the device's with no transform", "[device-pixels]")
{
    QImage image = canvas(1.5);
    QPainter painter(&image);

    REQUIRE(devicepixels::effectiveRatio(painter) == Catch::Approx(1.5));
}

// The case a scaled view makes: a ratio-2 screen with the table drawn at half
// size needs one pixel per table unit.
TEST_CASE("The effective ratio is the device's times the painter's scale", "[device-pixels]")
{
    QImage image = canvas(2.0);
    QPainter painter(&image);

    painter.setWorldTransform(QTransform::fromScale(0.5, 0.5));

    REQUIRE(devicepixels::effectiveRatio(painter) == Catch::Approx(1.0));
}

// A tilted card asks for the same pixels as a straight one, and a translation
// changes nothing.
TEST_CASE("The effective ratio ignores rotation and translation", "[device-pixels]")
{
    QImage image = canvas(1.0);
    QPainter painter(&image);

    QTransform transform;
    transform.translate(40.0, 25.0);
    transform.rotate(30.0);
    transform.scale(0.75, 0.75);

    painter.setWorldTransform(transform);

    REQUIRE(devicepixels::effectiveRatio(painter) == Catch::Approx(0.75));
}

// A RESIZE'S FREEZE, which every pixmap item on a frozen view gets without asking:
// the live ratio times the factor, until the freeze is lifted.
TEST_CASE("A frozen widget's painters get its live ratio times the freeze", "[device-pixels]")
{
    RatioProbe probe;
    probe.resize(32, 32);

    const qreal live = probe.devicePixelRatioF();

    devicepixels::setFreeze(probe, 0.5);
    probe.grab();

    REQUIRE(probe.seen.has_value());
    REQUIRE(*probe.seen == Catch::Approx(live * 0.5));

    devicepixels::setFreeze(probe, std::nullopt);
    probe.grab();

    REQUIRE(*probe.seen == Catch::Approx(live));
}

// The freeze is the widget's: a scene rendered into an image while its view is
// frozen is drawn at the image's own pixels.
TEST_CASE("A freeze on a widget leaves a painter on an image alone", "[device-pixels]")
{
    QWidget frozen;
    devicepixels::setFreeze(frozen, 0.5);

    QImage image = canvas(1.5);
    QPainter painter(&image);

    REQUIRE(devicepixels::effectiveRatio(painter) == Catch::Approx(1.5));
}

// THE DEVICE'S PIXELS, not the painter's units: at a ratio of 2 and a painter
// moved 0.3, the point 10.1 lands 20.8 pixels in, rounds to 21, and comes back as
// 10.2. A snap that ignored the ratio would give 9.7.
TEST_CASE("A snapped point lands on a whole device pixel", "[device-pixels]")
{
    QImage image = canvas(2.0);
    QPainter painter(&image);

    painter.translate(0.3, 0.0);

    const QPointF snapped = devicepixels::snapped(painter, QPointF(10.1, 4.0));

    REQUIRE(snapped.x() == Catch::Approx(10.2));
    REQUIRE(snapped.y() == Catch::Approx(4.0));
}

// A canvas covers its area from a corner on the grid: the origin's, or a whole
// number of pixels from an anchor - at a ratio where the two grids differ.
TEST_CASE("A canvas covers its area from a whole pixel of its anchor", "[device-pixels]")
{
    // 0.78 is a resized window's; 1.5 and 2 are the HiDPI screens' (phase 12).
    const qreal ratio = GENERATE(0.78, 1.5, 2.0);

    INFO("ratio " << ratio);

    const QRectF area(-60.3, -80.1, 120.6, 170.2);

    SECTION("anchored on the origin")
    {
        const devicepixels::Canvas made = devicepixels::canvas(area, ratio);

        REQUIRE(made.pixmap.devicePixelRatio() == Catch::Approx(ratio));
        REQUIRE(made.offset.x() * ratio == Catch::Approx(std::round(made.offset.x() * ratio)));
        REQUIRE(made.offset.y() * ratio == Catch::Approx(std::round(made.offset.y() * ratio)));

        const QRectF covered(made.offset, QSizeF(made.pixmap.size()) / ratio);
        REQUIRE(covered.contains(area));

        // And no more than a pixel past it on any side.
        REQUIRE(area.left() - covered.left() < 1.0 / ratio);
        REQUIRE(covered.right() - area.right() < 1.0 / ratio);

        REQUIRE(made.pixmap.toImage().pixelColor(0, 0).alpha() == 0);
    }

    SECTION("anchored on a point off the grid")
    {
        const QPointF anchor(-54.99, -79.0);
        const devicepixels::Canvas made = devicepixels::canvas(area, ratio, anchor);

        const QPointF steps = (made.offset - anchor) * ratio;

        REQUIRE(steps.x() == Catch::Approx(std::round(steps.x())));
        REQUIRE(steps.y() == Catch::Approx(std::round(steps.y())));
        REQUIRE(QRectF(made.offset, QSizeF(made.pixmap.size()) / ratio).contains(area));
    }

    SECTION("nothing for an empty area or ratio")
    {
        REQUIRE(devicepixels::canvas(QRectF(), ratio).pixmap.isNull());
        REQUIRE(devicepixels::canvas(area, 0.0).pixmap.isNull());
    }
}

// Ten-thousandths: the two ratios a resize really produced, which a hundredths
// key could not tell apart, are two keys - and two spellings of one ratio are one.
TEST_CASE("The ratio key tells near ratios apart and equal ones together", "[device-pixels]")
{
    REQUIRE(devicepixels::ratioKey(1.25) == 12500);
    REQUIRE(devicepixels::ratioKey(0.5649) != devicepixels::ratioKey(0.5551));
    REQUIRE(devicepixels::ratioKey(1.0) == devicepixels::ratioKey(1.0 + 1e-12));
}
