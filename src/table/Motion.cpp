#include "Motion.h"

#include <QPointF>

#include <algorithm>

motion::Pace motion::paceAt(int stop)
{
    return static_cast<Pace>(std::clamp(stop, 0, stopCount - 1));
}

qreal motion::factorFor(Pace pace)
{
    switch(pace)
    {
        case Pace::Off:    return 0.0;
        case Pace::Quick:  return 0.5;
        case Pace::Normal: return 1.0;
        case Pace::Slow:   return 1.6;
    }

    // Not reached with an enumerator, and a value cast from nowhere moves as
    // little as can be asked for.
    return 0.0;
}

int motion::flightDuration(Pace pace)
{
    return qRound(flightMs * factorFor(pace));
}

int motion::sweepDuration(Pace pace)
{
    return qRound(sweepMs * factorFor(pace));
}

// CSS's cubic-bezier(x1, y1, x2, y2) is a segment from (0, 0) to (1, 1) with these two
// control points, which is exactly what QEasingCurve::BezierSpline's first segment is.
namespace
{
QEasingCurve bezier(QPointF first, QPointF second)
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(first, second, QPointF(1.0, 1.0));

    return curve;
}
}

QEasingCurve motion::flightCurve()
{
    return bezier(QPointF(0.2, 0.75), QPointF(0.25, 1.0));
}

QEasingCurve motion::sweepCurve()
{
    return bezier(QPointF(0.5, 0.0), QPointF(0.75, 0.0));
}
