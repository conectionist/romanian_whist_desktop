#include "SeatLayout.h"

#include "table/TableSpace.h"

#include <array>
#include <cmath>
#include <numbers>

namespace
{

// SECTION 2.5'S TABLE (table.js LAYOUTS): the top-left corner of each panel, by
// player count, from the first seat clockwise of the bottom one. Hand-tuned in the
// mock-up, so copied rather than derived - SeatLayoutTests checks the properties
// that tuning was for, so an edit here that loses one fails by name.
//
// TWO PLAYERS IS NOT IN THE MOCK-UP. The opponent takes the top spot, which is the
// four- and six-player rows' top seat (open question Q7).
using Corners = std::array<QPointF, 5>;

constexpr std::array<Corners, 5> panelCorners = { {
    { { { 628.0, -140.0 } } },                                                  // 2
    { { { 96.0, 49.0 }, { 1160.0, 49.0 } } },                                   // 3
    { { { -34.0, 453.0 }, { 628.0, -140.0 }, { 1290.0, 453.0 } } },             // 4
    { { { -24.0, 611.0 }, { 96.0, 49.0 }, { 1160.0, 49.0 }, { 1280.0, 611.0 } } }, // 5
    { { { 1.0, 700.0 }, { 96.0, 49.0 }, { 628.0, -140.0 }, { 1160.0, 49.0 }, { 1255.0, 700.0 } } } // 6
} };

constexpr unsigned int fewestSeats = 2;
constexpr unsigned int mostSeats = fewestSeats + panelCorners.size() - 1;

// table.js buildSeats()'s slide: how far a rack keeps clear of its own panel, the
// step it slides by, how far it may slide, and the extra it takes once clear.
constexpr qreal rackClearance = 10.0;
constexpr qreal rackSlideStep = std::numbers::pi / 180.0;
constexpr int rackMostSteps = 90;
constexpr qreal rackSlideExtra = 9.0 * std::numbers::pi / 180.0;

}

bool seatlayout::humanSeated(unsigned int seatCount, int humanSeat)
{
    return humanSeat >= 0 && static_cast<unsigned int>(humanSeat) < seatCount;
}

unsigned int seatlayout::stepsFromBottom(unsigned int seatCount, int humanSeat, unsigned int seat)
{
    if(seatCount == 0)
        return 0;

    // Out of range is read as "nobody", not clamped and not wrapped: a seat
    // index that does not exist is a caller bug, and rotating the table by a
    // modulo of it would hide that behind a layout that merely looks odd.
    const unsigned int bottomSeat = humanSeated(seatCount, humanSeat) ? static_cast<unsigned int>(humanSeat)
                                                                      : 0u;

    // Unsigned arithmetic wraps, so the modulo is what keeps `seat` behind
    // `bottomSeat` at a positive offset rather than an enormous one.
    return (seat + seatCount - bottomSeat) % seatCount;
}

QRectF seatlayout::panelRect(unsigned int seatCount, int humanSeat, unsigned int seat)
{
    if(seatCount < fewestSeats || seatCount > mostSeats || seat >= seatCount)
        return QRectF();

    const unsigned int steps = stepsFromBottom(seatCount, humanSeat, seat);

    if(steps == 0)
        return tablespace::bottomPanel;

    return QRectF(panelCorners[seatCount - fewestSeats][steps - 1], tablespace::panelSize);
}

qreal seatlayout::rackAngle(QRectF panel)
{
    if(panel.isEmpty())
        return 0.0;

    const QPointF centre = panel.center();
    const QRectF clear = panel.adjusted(-rackClearance, -rackClearance, rackClearance, rackClearance);

    // STRICTLY INSIDE, as table.js's covered() compares: a rim point on the grown
    // panel's very edge is clear.
    const auto covered = [&](qreal angle)
    {
        const QPointF at = rackOrigin(angle);

        return at.x() > clear.left() && at.x() < clear.right()
               && at.y() > clear.top() && at.y() < clear.bottom();
    };

    qreal angle = std::atan2(centre.y() - tablespace::centre.y(), centre.x() - tablespace::centre.x());

    if(covered(angle))
    {
        // An increasing angle is clockwise on screen, because y is down.
        for(int step = 0 ; step < rackMostSteps && covered(angle) ; step++)
            angle += rackSlideStep;

        angle += rackSlideExtra;
    }

    return angle;
}

QPointF seatlayout::onCircle(qreal angle, qreal radius)
{
    return tablespace::centre + radius * QPointF(std::cos(angle), std::sin(angle));
}

QPointF seatlayout::rackOrigin(qreal angle)
{
    return onCircle(angle, tablespace::rackRadius);
}

std::optional<seatlayout::Place> seatlayout::place(unsigned int seatCount, int humanSeat, unsigned int seat)
{
    const QRectF panel = panelRect(seatCount, humanSeat, seat);

    if(panel.isEmpty())
        return std::nullopt;

    const bool human = humanSeated(seatCount, humanSeat) && static_cast<unsigned int>(humanSeat) == seat;

    return Place{ panel, human, human ? 0.0 : rackAngle(panel) };
}

seatlayout::Places seatlayout::places(unsigned int seatCount, int humanSeat)
{
    Places all;
    all.reserve(seatCount);

    for(unsigned int seat = 0 ; seat < seatCount ; seat++)
        all.push_back(place(seatCount, humanSeat, seat));

    return all;
}
