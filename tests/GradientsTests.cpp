#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "paint/Gradients.h"

#include <QColor>
#include <QGradientStops>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>

// CSS'S radial-gradient(), as the redesign draws it.
//
// THROUGH A TRANSPARENT STOP WITHOUT TURNING GREY. CSS blends gradient stops
// premultiplied, so white fading to `transparent` stays white as its alpha falls.
// Qt's default ColorInterpolation does the same - and the felt's vignette, which
// fades white out and black in through one transparent stop, relies on it.
// ComponentInterpolation would pass through grey halfway, which is what this
// would catch.
TEST_CASE("A gradient fades through a transparent stop without turning grey", "[gradients]")
{
    QImage image(101, 1, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QGradientStops stops = { { 0.0, QColor(255, 255, 255, 255) },
                                   { 1.0, QColor(Qt::transparent) } };

    // Centred at the left end, so the gradient runs along the row.
    {
        QPainter painter(&image);
        painter.fillRect(image.rect(), gradients::cssRadial(QRectF(0.0, 0.0, 101.0, 1.0),
                                                            QPointF(0.0, 0.5),
                                                            gradients::Shape::Circle, stops));
    }

    const QColor halfway = image.pixelColor(50, 0);

    REQUIRE(halfway.alphaF() == Catch::Approx(0.5).margin(0.03));
    REQUIRE(halfway.red() >= 250);
    REQUIRE(halfway.green() >= 250);
    REQUIRE(halfway.blue() >= 250);
}
