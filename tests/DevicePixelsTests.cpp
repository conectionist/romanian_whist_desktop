#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "paint/DevicePixels.h"

#include <QImage>
#include <QPainter>
#include <QTransform>

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

// Ten-thousandths: the two ratios a resize really produced, which a hundredths
// key could not tell apart, are two keys - and two spellings of one ratio are one.
TEST_CASE("The ratio key tells near ratios apart and equal ones together", "[device-pixels]")
{
    REQUIRE(devicepixels::ratioKey(1.25) == 12500);
    REQUIRE(devicepixels::ratioKey(0.5649) != devicepixels::ratioKey(0.5551));
    REQUIRE(devicepixels::ratioKey(1.0) == devicepixels::ratioKey(1.0 + 1e-12));
}
