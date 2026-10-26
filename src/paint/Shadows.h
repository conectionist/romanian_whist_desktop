#ifndef SHADOWS_H
#define SHADOWS_H

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QtGlobal>

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

// The Gaussian's standard deviation for CSS's blur radius.
constexpr qreal sigma(qreal blur)
{
    return blur / 2.0;
}

// HOW FAR A SHADOW REACHES past the edge it blurs, in the same units: three
// sigmas, where a Gaussian is under 0.2% of its peak - below a step of eight-bit
// alpha for any colour on the table. Every shadow here stops there, so anything
// sized to hold one is sized by this.
constexpr qreal reach(qreal blur)
{
    return 3.0 * sigma(blur);
}

// CSS's two kinds of box-shadow: outside the shape, or inside it - and the outer
// kind again, for a shape drawn opaque straight over it.
enum class Side
{
    Outer,
    Beneath,
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
//
// BENEATH IS OUTER FOR A CIRCLE THAT IS OPAQUE AND PAINTED OVER IT, the table's
// rim on the room: the shadow fills the whole disc, its inside at the edge's own
// value, so the circle laid over it meets one anti-aliased edge. Cut to a ring,
// the shadow's inner edge and the circle's would each half-cover the rim's
// pixels, and the room would show through the two of them more than through
// either.
void drawCircleShadow(QPainter& painter, QPointF centre, qreal radius, qreal blur,
                      const QColor& colour, Side side);

// Any other shape's shadow: the shape, moved by `offset`, blurred by `blur` and
// filled with `colour`.
//
// OUTER, the default, is a box-shadow: drawn outside the shape only, as CSS clips
// an outer shadow to outside the element's border box, which matters under the
// panels that are not quite opaque.
//
// BENEATH is a drop-shadow under an opaque shape painted straight over it, as
// drawCircleShadow()'s is: not cut away under the shape, so the shape's
// anti-aliased edge meets one shadow rather than a shadow cut by a second edge -
// which left a light seam round every card in a rack (PHASE5_REVIEW_FINDINGS.md
// #1). Inset is not drawn for shapes.
//
// RASTERIZED AND BLURRED: the shape's alpha at the pixels it lands on, padded by
// three sigmas, then three box blurs across and three down, which is the usual
// Gaussian approximation and costs the same at any blur.
void drawShapeShadow(QPainter& painter, const QPainterPath& shape, QPointF offset, qreal blur,
                     const QColor& colour, Side side = Side::Outer);

} // namespace shadows

#endif
