#ifndef DEVICE_PIXELS_H
#define DEVICE_PIXELS_H

#include <QtGlobal>

class QPainter;

// HOW MANY PIXELS A PIXMAP NEEDS, in one place.
//
// B1's crispness contract is that a pixmap is rasterized at exactly the pixels it
// will cover, so it is blitted 1:1. Before the table redesign's phase 2 that was
// the screen's device pixel ratio. Since then the view scales a fixed table space
// to the window, so it is the screen's ratio TIMES that scale: a card 225 table
// units tall at a view scale of 0.78 covers 175 pixels, and a pixmap made for 225
// is resampled down on its way there. Every pixmap blitted in the scene has to ask
// this question - cards now, the cached table, the avatar portraits and the rack
// cards in later phases - so it is answered here rather than by each of them.
namespace devicepixels
{

// The painter's device ratio times the scale of its world transform: physical
// pixels per unit of whatever the painter is drawing in.
//
// THE DEVICE'S RATIO, not a widget's. A scene rendered into an image has no widget,
// and the image's own ratio is the right answer there; a view's viewport is the
// device when painting on screen, and under QWidget::grab() as well.
//
// The scale is QStyleOptionGraphicsItem::levelOfDetailFromTransform(), which
// ignores rotation and translation - a tilted card asks for the same pixels as a
// straight one.
qreal effectiveRatio(const QPainter& painter);

// A ratio as a cache key, quantized to TEN-THOUSANDTHS.
//
// A screen reports 1, 1.25, 1.5 or 2, but the ratio above is continuous: two
// window sizes a few pixels apart give ratios a hundredth apart, and a key that
// rounded them together served one the other's pixmap - the wrong size, stamped
// with the wrong ratio, and so resampled after all. At ten-thousandths two ratios
// sharing a key differ by well under a hundredth of a pixel on any card, while two
// bit patterns of the same ratio still compare equal.
int ratioKey(qreal ratio);

} // namespace devicepixels

#endif
