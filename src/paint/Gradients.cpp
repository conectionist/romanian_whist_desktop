#include "Gradients.h"

#include <QRadialGradient>
#include <QTransform>

#include <algorithm>
#include <cmath>

QBrush gradients::cssRadial(const QRectF& box, QPointF at, Shape shape, const QGradientStops& stops)
{
    const QPointF centre(box.left() + box.width() * at.x(), box.top() + box.height() * at.y());

    // The distances to the farthest side across and down, which are also the
    // offsets of the farthest corner.
    const qreal across = std::max(centre.x() - box.left(), box.right() - centre.x());
    const qreal down = std::max(centre.y() - box.top(), box.bottom() - centre.y());

    qreal radiusX = std::hypot(across, down);
    qreal radiusY = radiusX;

    if(shape == Shape::Ellipse)
    {
        // farthest-side's aspect, grown through the corner: the corner sits at
        // (across, down), so the scale that puts it on the ellipse is sqrt(2).
        radiusX = std::sqrt(2.0) * across;
        radiusY = std::sqrt(2.0) * down;
    }

    // QRadialGradient is a circle, so the shape is a unit circle in the brush's
    // transform, scaled to the two radii.
    QRadialGradient gradient(QPointF(0.0, 0.0), 1.0);
    gradient.setStops(stops);

    QTransform transform = QTransform::fromTranslate(centre.x(), centre.y());
    transform.scale(std::max(radiusX, 1e-9), std::max(radiusY, 1e-9));

    QBrush brush(gradient);
    brush.setTransform(transform);

    return brush;
}
