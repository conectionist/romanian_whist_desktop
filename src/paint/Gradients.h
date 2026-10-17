#ifndef GRADIENTS_H
#define GRADIENTS_H

#include <QBrush>
#include <QGradientStops>
#include <QPointF>
#include <QRectF>

// CSS'S radial-gradient(), ported once.
//
// The mock-up draws its room, its felt's vignette and its logo plaque with
// radial-gradient() and no size, and CSS's default size - farthest-corner - is
// the part that is easy to get wrong: the declaration never spells it out, and a
// gradient sized to the box's half-width puts its last stop well inside the box
// rather than at its corners. The redesign's phase 2 derived it for the room and
// phase 3 needed it twice more, so it lives here rather than in each painter.
namespace gradients
{

// `ellipse` or `circle`, the radial-gradient() shape keyword.
enum class Shape
{
    Ellipse,
    Circle
};

// A brush painting CSS's `radial-gradient(<shape> at <at>, <stops>)` over `box`,
// sized farthest-corner. `at` is the centre as fractions of the box, as CSS
// writes it: (0.5, 0.42) is `at 50% 42%`. The stops are CSS's, 0 at the centre
// and 1 at the ending shape.
//
// FARTHEST-CORNER: a circle reaches the box's corner farthest from the centre. An
// ellipse takes the aspect it would have at farthest-side - the distance to the
// farthest side across, and to the farthest side down - and grows until it passes
// through that same corner, which makes both radii farthest-side's times the
// square root of two.
QBrush cssRadial(const QRectF& box, QPointF at, Shape shape, const QGradientStops& stops);

} // namespace gradients

#endif
