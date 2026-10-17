#ifndef SHADOWS_H
#define SHADOWS_H

#include <QColor>
#include <QPainterPath>
#include <QPointF>

class QPainter;

// CSS'S box-shadow, ported once: the table's shadow and its inset ones in the
// redesign's phase 3, every panel's drop shadow and the gold glows after it.
//
// A BLUR RADIUS IS CSS'S, not a Gaussian's. CSS defines the blur of a shadow with
// blur radius b as a Gaussian of standard deviation b / 2, so "0 0 70px" is a
// sigma of 35. Every function here takes CSS's number, so a value copied from
// page.html is copied as it is written.
//
// IN THE PAINTER'S UNITS, whatever its transform: table units when a picture of
// the table is being painted. Anything rasterized is rasterized at the pixels it
// lands on, by devicepixels::effectiveRatio().
namespace shadows
{

// CSS's two kinds of box-shadow: outside the shape, or inside it.
enum class Side
{
    Outer,
    Inset
};

// A circle's box-shadow with no offset and no spread: the table's shadow on the
// room, the rim's into the wood, the felt's into the felt.
//
// ANALYTIC, NOT BLURRED. Across a straight edge a Gaussian blur of a step is the
// error function, alpha = a * erfc(d / (sigma * sqrt 2)) / 2 at a distance d from
// the edge - half the colour on the edge itself. A circle of radius r is that
// straight edge to within a curvature error of order sigma / r, and every circle
// on the table is at least seven sigmas across, so the profile drawn as a radial
// gradient is the blur to well under a step of eight-bit alpha - and costs a
// gradient fill rather than a blur of a picture the size of the table.
//
// Outer is drawn outside the circle only and Inset inside it only, as CSS clips
// them.
void drawCircleShadow(QPainter& painter, QPointF centre, qreal radius, qreal blur,
                      const QColor& colour, Side side);

// Any other shape's outer box-shadow: the shape, moved by `offset`, blurred by
// `blur` and filled with `colour` - drawn outside the shape only, as CSS clips an
// outer shadow to outside the element's border box, which matters under the
// panels that are not quite opaque.
//
// RASTERIZED AND BLURRED: the shape's alpha at the pixels it lands on, padded by
// three sigmas, then three box blurs across and three down, which is the usual
// Gaussian approximation and costs the same at any blur.
void drawShapeShadow(QPainter& painter, const QPainterPath& shape, QPointF offset, qreal blur,
                     const QColor& colour);

} // namespace shadows

#endif
