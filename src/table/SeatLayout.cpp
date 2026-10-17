#include "SeatLayout.h"

#include <cmath>
#include <numbers>

namespace
{

// How much of the rect the ellipse of seats spans. Just inside it rather than
// filling it, so a seat never sits exactly on the boundary.
//
// It is NOT reduced to leave room for the plaque around each centre. This
// function is handed the rect a seat's CENTRE may occupy, and choosing that rect
// is the caller's business - TableScene's, which hands it a fixed ring through
// ringRect() below, at the radii that put each plaque where section 2.5 does.
// Doing it here would mean this function knowing what is drawn at the points it
// returns.
constexpr qreal radiusFractionX = 0.46;
constexpr qreal radiusFractionY = 0.44;

}

// SECTION 14'S ENTIRE ANGLE TABLE IS THIS ONE FORMULA, which is worth saying
// because the table is written as five rows and reads like five special cases:
//
//     2 | human bottom; bot top
//     3 | human bottom; bots top-left, top-right
//     4 | human bottom; bots left, top, right
//     5 | human bottom; bots bottom-left, top-left, top-right, bottom-right
//     6 | human bottom; bots bottom-left, top-left, top, top-right, bottom-right
//
// Put the k-th seat clockwise from the human at theta = k * 360/n, with theta
// measured from bottom-centre, and every row falls out. Four seats: 90, 180, 270
// give left, top, right. Five: 72 and 144 give bottom-left and top-left, 216 and
// 288 their mirrors. Six: 60-degree steps give bottom-left, top-left, top,
// top-right, bottom-right. All five checked before this was written, and
// SeatLayoutTests.cpp asserts each row against the words above rather than
// against these numbers - so the test is the document, not a copy of the code.
//
// Screen coordinates have y DOWNWARD, which is why cos is added to y rather than
// subtracted and sin is subtracted from x. At theta = 0 that is (cx, cy + ry),
// the bottom; at 90 it is (cx - rx, cy), the left. Left after bottom is what
// makes the sequence clockwise on screen - the same direction 6, 9, 12, 3 runs
// on a clock face.
std::vector<QPointF> seatlayout::seatCentres(unsigned int seatCount, int humanSeat, QRectF table)
{
    if(seatCount == 0 || table.isEmpty())
        return {};

    // Out of range is read as "nobody", not clamped and not wrapped: a seat
    // index that does not exist is a caller bug, and rotating the table by a
    // modulo of it would hide that behind a layout that merely looks odd.
    const bool seated = humanSeat >= 0 && static_cast<unsigned int>(humanSeat) < seatCount;
    const unsigned int bottomSeat = seated ? static_cast<unsigned int>(humanSeat) : 0u;

    const QPointF centre = table.center();
    const qreal radiusX = table.width() * radiusFractionX;
    const qreal radiusY = table.height() * radiusFractionY;

    const qreal step = 2.0 * std::numbers::pi / seatCount;

    std::vector<QPointF> centres(seatCount);

    for(unsigned int seat = 0 ; seat < seatCount ; seat++)
    {
        // Steps CLOCKWISE FROM THE HUMAN in seat order, so the seat after this
        // one on screen is the seat that plays after it. Unsigned arithmetic
        // wraps, so the modulo is what keeps `seat` behind `bottomSeat` at a
        // positive offset rather than an enormous one.
        const unsigned int stepsFromBottom = (seat + seatCount - bottomSeat) % seatCount;
        const qreal theta = stepsFromBottom * step;

        centres[seat] = QPointF(centre.x() - radiusX * std::sin(theta),
                                centre.y() + radiusY * std::cos(theta));
    }

    return centres;
}

// The inverse of the radius fractions above: hand the result to seatCentres()
// and it lays a ring of exactly `radius` about `centre`. Dividing by each
// fraction separately is what makes it a circle rather than an ellipse of the
// window's shape - or, with two radii, an ellipse of the shape asked for.
QRectF seatlayout::ringRect(QPointF centre, qreal radius)
{
    return ringRect(centre, radius, radius);
}

QRectF seatlayout::ringRect(QPointF centre, qreal radiusX, qreal radiusY)
{
    if(radiusX <= 0.0 || radiusY <= 0.0)
        return QRectF();

    // seatCentres() takes a FRACTION of the rect's full width as the radius, so
    // the rect is the radius divided by that fraction - not twice it. Getting
    // this wrong produces a ring of exactly double the radius asked for, which
    // looks like a plausible table and is silently wrong; the test that
    // measures the radius back out is what caught it.
    const qreal width = radiusX / radiusFractionX;
    const qreal height = radiusY / radiusFractionY;

    return QRectF(centre.x() - width / 2.0, centre.y() - height / 2.0, width, height);
}
