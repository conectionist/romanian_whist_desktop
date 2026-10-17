#include "DevicePixels.h"

#include <QPaintDevice>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QVariant>
#include <QWidget>

namespace
{

// The dynamic property a freeze is kept in. Private to this file, so setFreeze()
// is the only way to put one there.
constexpr const char* freezeProperty = "devicepixels.freeze";

}

qreal devicepixels::effectiveRatio(const QPainter& painter)
{
    // No device is a painter that is not painting; 1.0 is the honest answer there.
    const QPaintDevice* device = painter.device();
    const qreal deviceRatio = device ? device->devicePixelRatioF() : 1.0;

    const qreal live = deviceRatio
                     * QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter.worldTransform());

    if(!device || device->devType() != QInternal::Widget)
        return live;

    const QVariant factor = static_cast<const QWidget*>(device)->property(freezeProperty);

    return factor.isValid() ? live * factor.toReal() : live;
}

void devicepixels::setFreeze(QWidget& widget, std::optional<qreal> factor)
{
    // An invalid QVariant removes the property, so a thawed widget carries nothing.
    widget.setProperty(freezeProperty, factor ? QVariant(*factor) : QVariant());
}

int devicepixels::ratioKey(qreal ratio)
{
    return qRound(ratio * 10000.0);
}
