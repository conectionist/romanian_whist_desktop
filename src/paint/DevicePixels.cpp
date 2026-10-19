#include "DevicePixels.h"

#include <QPaintDevice>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTransform>
#include <QVariant>
#include <QWidget>

#include <cmath>

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

QPointF devicepixels::snapped(const QPainter& painter, QPointF point)
{
    // The device transform carries the device's ratio as well as the painter's
    // own transform, so this is the point in the device's actual pixels.
    const QTransform toDevice = painter.deviceTransform();
    const QPointF onDevice = toDevice.map(point);

    return toDevice.inverted().map(QPointF(std::round(onDevice.x()), std::round(onDevice.y())));
}

devicepixels::Canvas devicepixels::canvas(const QRectF& area, qreal ratio, QPointF anchor)
{
    if(area.isEmpty() || ratio <= 0.0)
        return {};

    const QPointF corner(anchor.x() + std::floor((area.left() - anchor.x()) * ratio) / ratio,
                         anchor.y() + std::floor((area.top() - anchor.y()) * ratio) / ratio);

    QPixmap pixmap(QSize(static_cast<int>(std::ceil((area.right() - corner.x()) * ratio)),
                         static_cast<int>(std::ceil((area.bottom() - corner.y()) * ratio))));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    return { pixmap, corner };
}

int devicepixels::ratioKey(qreal ratio)
{
    return qRound(ratio * 10000.0);
}
