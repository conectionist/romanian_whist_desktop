#include "DevicePixels.h"

#include <QPaintDevice>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

qreal devicepixels::effectiveRatio(const QPainter& painter, std::optional<qreal> frozen)
{
    // No device is a painter that is not painting; 1.0 is the honest answer there.
    const QPaintDevice* device = painter.device();
    const qreal deviceRatio = device ? device->devicePixelRatioF() : 1.0;

    if(frozen)
        return deviceRatio * *frozen;

    return deviceRatio
         * QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter.worldTransform());
}

int devicepixels::ratioKey(qreal ratio)
{
    return qRound(ratio * 10000.0);
}
