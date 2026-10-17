#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "paint/Shadows.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include <QtMath>

#include <cmath>

// CSS'S box-shadow, as the redesign draws it: a blur radius is a Gaussian of half
// that standard deviation, and across a straight edge a blurred step is the error
// function - half the colour on the edge, about 16% one sigma out, nothing by
// three. Both kinds of shadow are held to that profile, with the alpha read back
// off what they actually painted.

namespace
{

// Black at full alpha, so the alpha painted is the profile itself.
const QColor black(0, 0, 0, 255);

// A canvas big enough for the shapes below, transparent so only the shadow shows.
QImage canvas()
{
    QImage image(400, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

qreal alphaAt(const QImage& image, QPointF at)
{
    return image.pixelColor(qRound(at.x()), qRound(at.y())).alphaF();
}

// The Gaussian edge profile at a distance d from the edge, for a blur radius b.
qreal profile(qreal d, qreal blur)
{
    const qreal sigma = blur / 2.0;
    return 0.5 * std::erfc(d / (sigma * std::sqrt(2.0)));
}

const QPointF centre(200.0, 200.0);
constexpr qreal radius = 120.0;
constexpr qreal blur = 40.0;

}

TEST_CASE("A circle's outer shadow is the Gaussian edge profile", "[shadows]")
{
    QImage image = canvas();

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        shadows::drawCircleShadow(painter, centre, radius, blur, black, shadows::Side::Outer);
    }

    // Along a radius to the right, a little past the edge so the anti-aliased rim
    // of the ring is not what is read.
    for(const qreal d : { 2.0, 10.0, 20.0, 35.0 })
    {
        INFO("distance " << d);
        REQUIRE(alphaAt(image, centre + QPointF(radius + d, 0.0))
                == Catch::Approx(profile(d, blur)).margin(0.02));
    }

    // Faded to nothing at three sigmas, and nothing at all inside the circle: CSS
    // clips an outer shadow to outside its shape.
    REQUIRE(alphaAt(image, centre + QPointF(radius + 62.0, 0.0)) < 0.01);
    REQUIRE(alphaAt(image, centre + QPointF(radius - 5.0, 0.0)) == 0.0);
}

TEST_CASE("A circle's inset shadow mirrors the outer one inside the circle", "[shadows]")
{
    QImage image = canvas();

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        shadows::drawCircleShadow(painter, centre, radius, blur, black, shadows::Side::Inset);
    }

    for(const qreal d : { 2.0, 10.0, 20.0, 35.0 })
    {
        INFO("distance " << d);
        REQUIRE(alphaAt(image, centre - QPointF(radius - d, 0.0))
                == Catch::Approx(profile(d, blur)).margin(0.02));
    }

    // Nothing in the middle, and nothing outside the circle.
    REQUIRE(alphaAt(image, centre) < 0.01);
    REQUIRE(alphaAt(image, centre + QPointF(radius + 5.0, 0.0)) == 0.0);
}

// BENEATH, for an opaque circle painted over its own shadow - the table's rim.
// The shadow fills the disc, its inside at the edge's value, so the circle over it
// meets one anti-aliased edge. A ring's inner edge under the circle's would leave
// the rim's pixels each half-covered twice, and more of what is below showing
// through them than through either.
TEST_CASE("A circle over its shadow from beneath meets it in one edge", "[shadows]")
{
    const QColor shade(0, 0, 0, 191);
    const qreal edgeAlpha = shade.alphaF() * 0.5;

    // Inside, the edge's own value, all the way in.
    {
        QImage image = canvas();
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        shadows::drawCircleShadow(painter, centre, radius, blur, shade, shadows::Side::Beneath);
        painter.end();

        REQUIRE(alphaAt(image, centre) == Catch::Approx(edgeAlpha).margin(0.01));
        REQUIRE(alphaAt(image, centre + QPointF(radius - 5.0, 0.0))
                == Catch::Approx(edgeAlpha).margin(0.01));
    }

    // And the circle's edge through the middle of a pixel: half covered by the
    // circle, the rest by the shadow at its edge's value, so 1 - (1 - c)(1 - a).
    // A ring there would leave about 0.59 where this is 0.69.
    QImage image = canvas();

    const qreal halfPixel = radius + 0.5;
    const QPointF onRow(centre.x(), centre.y() + 0.5);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        shadows::drawCircleShadow(painter, onRow, halfPixel, blur, shade, shadows::Side::Beneath);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(onRow, halfPixel, halfPixel);
    }

    const qreal rim = image.pixelColor(qFloor(onRow.x() + halfPixel), qFloor(onRow.y())).alphaF();

    REQUIRE(rim == Catch::Approx(1.0 - 0.5 * (1.0 - edgeAlpha)).margin(0.03));
}

TEST_CASE("A shape's shadow is the same profile, outside the shape only", "[shadows]")
{
    QImage image = canvas();

    // A square whose right edge is a long straight edge at x = 200.
    QPainterPath square;
    square.addRect(QRectF(40.0, 40.0, 160.0, 320.0));

    {
        QPainter painter(&image);
        shadows::drawShapeShadow(painter, square, QPointF(), blur, black);
    }

    // Out from the middle of the right edge, the blurred step: the box blurs
    // approximate the Gaussian, so the margin is a little wider than the
    // analytic case's.
    for(const qreal d : { 3.0, 10.0, 20.0 })
    {
        INFO("distance " << d);
        REQUIRE(alphaAt(image, QPointF(200.0 + d, 200.0))
                == Catch::Approx(profile(d, blur)).margin(0.04));
    }

    REQUIRE(alphaAt(image, QPointF(200.0 + 70.0, 200.0)) < 0.01);

    // The shape itself is not shadowed: an outer shadow lies outside it, which is
    // what lets a panel that is not quite opaque sit over its own.
    REQUIRE(alphaAt(image, QPointF(120.0, 200.0)) < 0.01);
}

TEST_CASE("A shape's shadow moves with its offset", "[shadows]")
{
    QImage image = canvas();

    QPainterPath box;
    box.addRect(QRectF(100.0, 100.0, 100.0, 100.0));

    {
        QPainter painter(&image);
        shadows::drawShapeShadow(painter, box, QPointF(0.0, 40.0), 10.0, black);
    }

    // Below the shape it is the full colour - the moved shape lies there, far
    // from its blurred edge - and above it, where the moved shape does not reach,
    // there is none.
    REQUIRE(alphaAt(image, QPointF(150.0, 225.0)) == Catch::Approx(1.0).margin(0.02));
    REQUIRE(alphaAt(image, QPointF(150.0, 90.0)) < 0.01);
}

TEST_CASE("A shadow is rasterized at the pixels it lands on", "[shadows]")
{
    // At a ratio of 2 the same shadow covers twice the pixels, and its profile
    // lands at the same LOGICAL distances - the mask is made at the device's
    // resolution, not at the painter's units and then stretched.
    QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    image.fill(Qt::transparent);

    QPainterPath square;
    square.addRect(QRectF(40.0, 40.0, 160.0, 320.0));

    {
        QPainter painter(&image);
        shadows::drawShapeShadow(painter, square, QPointF(), blur, black);
    }

    // Read back in physical pixels: 10 logical units out is 20 pixels.
    REQUIRE(image.pixelColor(400 + 20, 400).alphaF()
            == Catch::Approx(profile(10.0, blur)).margin(0.04));
}

// OFF THE PIXEL GRID, the shadow and its cut-out land where the shape does. A
// shape whose left edge falls 0.3 into a pixel and whose right edge falls 0.7 into
// one has rims that mirror each other, so its shadow does too - but only if the
// shadow's picture is laid on the device's pixels. Laid at the fraction and
// rounded by the blit, the whole shadow slides, the hole with it, toward one side.
TEST_CASE("A shape's shadow off the pixel grid is laid where the shape is", "[shadows]")
{
    QImage image = canvas();

    QPainterPath box;
    box.addRect(QRectF(100.0, 100.0, 120.4, 120.0));

    {
        QPainter painter(&image);
        painter.translate(0.3, 0.6);
        shadows::drawShapeShadow(painter, box, QPointF(), 10.0, black);
    }

    // Along a row through the middle: pixel 100 holds the left rim, 220 the right.
    for(int k = 0 ; k <= 8 ; k++)
    {
        INFO(k << " pixels out from the rim");
        REQUIRE(image.pixelColor(100 - k, 160).alphaF()
                == Catch::Approx(image.pixelColor(220 + k, 160).alphaF()).margin(0.01));
    }
}
