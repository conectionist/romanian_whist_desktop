#include "Shadows.h"

#include "paint/DevicePixels.h"
#include "paint/Paths.h"

#include <QGradientStops>
#include <QImage>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{

// How finely the circle's profile is sampled. The error function is smooth, and
// two dozen stops over three sigmas put the gradient's straight segments within a
// fraction of a step of alpha of the curve.
constexpr int profileSteps = 24;

// The three box widths whose successive blurs approximate a Gaussian of `sigma`,
// in pixels: the standard derivation, which makes the variance of the three boxes
// together equal sigma squared using the two odd widths nearest the ideal one.
std::array<int, 3> boxWidths(qreal sigma)
{
    constexpr int passes = 3;

    const qreal ideal = std::sqrt(12.0 * sigma * sigma / passes + 1.0);

    int lower = static_cast<int>(std::floor(ideal));

    if(lower % 2 == 0)
        lower--;

    lower = std::max(1, lower);

    const int upper = lower + 2;
    const qreal lowerCount = (12.0 * sigma * sigma - passes * lower * lower - 4.0 * passes * lower
                              - 3.0 * passes)
                             / (-4.0 * lower - 4.0);
    const int count = qRound(lowerCount);

    std::array<int, 3> widths{};

    for(int i = 0 ; i < passes ; i++)
        widths[i] = i < count ? lower : upper;

    return widths;
}

// One box blur along rows (`stride` 1, `step` the row width) or columns (the other
// way round), over `lines` lines of `length` samples. Outside the picture counts as
// transparent, which is what the padding round the shape is for.
void boxBlur(std::vector<float>& samples, int length, int lines, int lineStride, int sampleStride,
             int width)
{
    const int radius = (width - 1) / 2;

    if(radius <= 0)
        return;

    std::vector<float> line(static_cast<std::size_t>(length));

    for(int l = 0 ; l < lines ; l++)
    {
        float* first = samples.data() + static_cast<std::ptrdiff_t>(l) * lineStride;

        for(int i = 0 ; i < length ; i++)
            line[static_cast<std::size_t>(i)] = first[static_cast<std::ptrdiff_t>(i) * sampleStride];

        // A running sum over the window [i - radius, i + radius].
        float sum = 0.0f;

        for(int i = 0 ; i <= std::min(radius, length - 1) ; i++)
            sum += line[static_cast<std::size_t>(i)];

        for(int i = 0 ; i < length ; i++)
        {
            first[static_cast<std::ptrdiff_t>(i) * sampleStride] = sum / static_cast<float>(width);

            const int entering = i + radius + 1;
            const int leaving = i - radius;

            if(entering < length)
                sum += line[static_cast<std::size_t>(entering)];

            if(leaving >= 0)
                sum -= line[static_cast<std::size_t>(leaving)];
        }
    }
}

// The shape's coverage, 0 to 1 per pixel, over `pixels` physical pixels laid on
// `bounds` (painter units) at `ratio` pixels per unit.
std::vector<float> coverage(const QPainterPath& shape, QPointF offset, const QRectF& bounds,
                            QSize pixels, qreal ratio)
{
    QImage mask(pixels, QImage::Format_Alpha8);
    mask.fill(0);

    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(ratio, ratio);
        painter.translate(offset - bounds.topLeft());
        painter.fillPath(shape, Qt::black);
    }

    std::vector<float> samples(static_cast<std::size_t>(pixels.width()) * pixels.height());

    for(int y = 0 ; y < pixels.height() ; y++)
    {
        const uchar* row = mask.constScanLine(y);

        for(int x = 0 ; x < pixels.width() ; x++)
            samples[static_cast<std::size_t>(y) * pixels.width() + x] = row[x] / 255.0f;
    }

    return samples;
}

}

void shadows::drawCircleShadow(QPainter& painter, QPointF centre, qreal radius, qreal blur,
                               const QColor& colour, Side side)
{
    if(radius <= 0.0 || blur <= 0.0 || colour.alpha() == 0)
        return;

    const qreal sigma = shadows::sigma(blur);
    const qreal reach = shadows::reach(blur);

    // The ring the profile runs across: from the circle outward, or inward from it.
    const bool outside = side != Side::Inset;
    const qreal inner = outside ? radius : std::max(0.0, radius - reach);
    const qreal outer = outside ? radius + reach : radius;

    QGradientStops stops;

    for(int i = 0 ; i <= profileSteps ; i++)
    {
        const qreal at = inner + (outer - inner) * i / profileSteps;
        const qreal fromEdge = outside ? at - radius : radius - at;

        QColor sample = colour;
        sample.setAlphaF(colour.alphaF() * 0.5 * std::erfc(fromEdge / (sigma * std::sqrt(2.0))));

        stops.append(QGradientStop(at / outer, sample));
    }

    QRadialGradient gradient(centre, outer);
    gradient.setStops(stops);

    // The ring itself, so an outer shadow never reaches inside the circle and an
    // inset one never outside it - or, beneath a circle that will cover it, the
    // whole disc: inside the profile's first stop the gradient pads with that
    // stop, the edge's own value, and the circle hides it.
    const QPainterPath area = side == Side::Beneath ? paths::disc(centre, outer)
                                                    : paths::ring(centre, inner, outer);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawPath(area);
    painter.restore();
}

void shadows::drawShapeShadow(QPainter& painter, const QPainterPath& shape, QPointF offset,
                              qreal blur, const QColor& colour)
{
    if(shape.isEmpty() || colour.alpha() == 0)
        return;

    const qreal ratio = devicepixels::effectiveRatio(painter);

    if(ratio <= 0.0)
        return;

    // A negative blur is no blur, as CSS has it.
    const qreal sigma = shadows::sigma(std::max(0.0, blur));
    const qreal reach = shadows::reach(std::max(0.0, blur));

    // Where the shadow can fall, in the painter's units: the moved shape, grown by
    // the blur's reach.
    const QRectF bounds =
        shape.boundingRect().translated(offset).adjusted(-reach, -reach, reach, reach);

    const QSize pixels(std::max(1, qCeil(bounds.width() * ratio)),
                       std::max(1, qCeil(bounds.height() * ratio)));

    std::vector<float> shadow = coverage(shape, offset, bounds, pixels, ratio);

    // The blur, in pixels: three boxes across, then three down.
    for(const int width : boxWidths(sigma * ratio))
        boxBlur(shadow, pixels.width(), pixels.height(), pixels.width(), 1, width);

    for(const int width : boxWidths(sigma * ratio))
        boxBlur(shadow, pixels.height(), pixels.width(), 1, pixels.width(), width);

    // OUTSIDE THE SHAPE ONLY, as CSS clips an outer shadow: the blurred shadow
    // times what the unmoved shape leaves uncovered. Its own anti-aliased coverage
    // rather than a clip path, which the raster engine draws aliased - and under a
    // panel that is not quite opaque, the edge would show.
    const std::vector<float> hole = coverage(shape, QPointF(), bounds, pixels, ratio);

    QImage picture(pixels, QImage::Format_ARGB32_Premultiplied);

    for(int y = 0 ; y < pixels.height() ; y++)
    {
        QRgb* row = reinterpret_cast<QRgb*>(picture.scanLine(y));

        for(int x = 0 ; x < pixels.width() ; x++)
        {
            const std::size_t at = static_cast<std::size_t>(y) * pixels.width() + x;
            const qreal alpha = colour.alphaF() * shadow[at] * (1.0f - hole[at]);

            row[x] = qPremultiply(qRgba(colour.red(), colour.green(), colour.blue(),
                                        qBound(0, qRound(alpha * 255.0), 255)));
        }
    }

    // Laid out at `bounds` and carrying the ratio it was rasterized at, so it is
    // blitted 1:1.
    picture.setDevicePixelRatio(ratio);

    painter.drawImage(bounds.topLeft(), picture);
}
