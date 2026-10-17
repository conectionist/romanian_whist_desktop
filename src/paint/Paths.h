#ifndef PATHS_H
#define PATHS_H

#include <QPainterPath>
#include <QPointF>
#include <QtGlobal>

// THE ROUND SHAPES THE TABLE IS DRAWN FROM, built in one place: the shadows'
// rings and the table's discs and bands are the same two paths.
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

} // namespace paths

#endif
