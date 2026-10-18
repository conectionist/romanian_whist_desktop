#ifndef PATHS_H
#define PATHS_H

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QtGlobal>

// THE ROUND SHAPES THE TABLE IS DRAWN FROM, built in one place: the shadows'
// rings and the table's discs and bands are the same two paths, and every panel
// - the logo, the seats, later the HUD and the sheets - is the rounded pair.
namespace paths
{

inline QPainterPath disc(QPointF centre, qreal radius)
{
    QPainterPath path;
    path.addEllipse(centre, radius, radius);
    return path;
}

// The band between two circles about one centre: both in one path, which
// QPainterPath fills odd-even.
inline QPainterPath ring(QPointF centre, qreal inner, qreal outer)
{
    QPainterPath path = disc(centre, outer);
    path.addEllipse(centre, inner, inner);
    return path;
}

inline QPainterPath rounded(const QRectF& rect, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

// The band between two rounded rects, filled odd-even as ring() is: a panel's
// inset line, or a ring drawn round one.
inline QPainterPath roundedBand(const QRectF& outer, qreal outerRadius, const QRectF& inner,
                                qreal innerRadius)
{
    QPainterPath path = rounded(outer, outerRadius);
    path.addRoundedRect(inner, innerRadius, innerRadius);
    return path;
}

} // namespace paths

#endif
