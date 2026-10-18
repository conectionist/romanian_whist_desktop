#ifndef DEVICE_PIXELS_H
#define DEVICE_PIXELS_H

#include <QPointF>
#include <QtGlobal>

#include <optional>

class QPainter;
class QWidget;

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
//
// TIMES A FREEZE, when the device is a widget a resize has frozen (setFreeze()
// below). Without one it is exactly the ratio above.
qreal effectiveRatio(const QPainter& painter);

// FREEZES WHAT effectiveRatio() ANSWERS FOR A WIDGET, while a window is being
// resized (TableView). Each step of a drag changes the view's scale, so every
// pixmap asked for at the live one would miss its cache and be rasterized again,
// every frame. With a freeze, a painter on `widget` is answered with its live ratio
// times `factor` - the scale the resize began from over the live one - so the
// pixmaps already made are asked for again, and Qt scales them to where they now
// land. Nothing clears it but this, with std::nullopt.
//
// ON THE WIDGET, NOT THE SCENE, AND APPLIED HERE RATHER THAN BY THE ITEMS. Every
// pixmap item on the table - the cards and the table now, the portraits and the
// rack cards in later phases - asks effectiveRatio() anyway, so none of them can
// forget the freeze. A scene rendered into an image paints on the image, not on
// the widget, so it gets that image's own ratio, frozen or not; and the freeze
// dies with the widget, whatever happens to the scene.
//
// A FACTOR, not a scale to put in place of the live one, so an item drawn at a
// scale of its own keeps it.
void setFreeze(QWidget& widget, std::optional<qreal> factor);

// `point`, moved by under half a pixel so that it lands on a WHOLE DEVICE PIXEL
// through the painter's transform and the device's ratio. A picture laid at a
// fraction of a pixel is sampled between its pixels or rounded by the blit,
// either of which slides it off what it was made to line up with - a texture's
// grain blurred, a shadow's cut-out beside its shape. Exact for a painter that
// scales and translates, and for one turned a quarter turn as well (a rack). Any
// other turn puts the point on a pixel but the picture is resampled anyway.
QPointF snapped(const QPainter& painter, QPointF point);

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
