#include "TableButtonItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

namespace
{

// page.html's .btn and .panel.btn, in table units.
constexpr qreal borderWidth = 1.0;
constexpr qreal paddingX = 22.0;
constexpr qreal iconSize = 24.0;
constexpr qreal iconGap = 12.0;

QFont labelFont()
{
    QFont font = fonts::heading();
    font.setPixelSize(tablebutton::labelSize);
    return font;
}

}

QSizeF tablebutton::sizeFor(const QString& label)
{
    const qreal labelWidth = QFontMetricsF(labelFont()).horizontalAdvance(label);

    return QSizeF(2.0 * (borderWidth + paddingX) + iconSize + iconGap + labelWidth, height);
}

TableButtonItem::TableButtonItem(icons::Icon _icon, const QString& label, panel::Cache* _panels,
                                 QGraphicsItem* parent)
    : QGraphicsObject(parent),
      icon(_icon),
      text(label),
      buttonSize(tablebutton::sizeFor(label)),
      panels(_panels ? _panels : &ownPanels)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void TableButtonItem::setTheme(const Theme& palette)
{
    if(theme.table == palette.table)
        return;

    theme = palette;
    memo.invalidate();
    update();
}

QRectF TableButtonItem::boundingRect() const
{
    return QRectF(QPointF(), buttonSize).marginsAdded(panel::reach());
}

QPainterPath TableButtonItem::shape() const
{
    return panel::shape(QRectF(QPointF(), buttonSize));
}

void TableButtonItem::setHovered(bool now)
{
    if(hovered == now)
        return;

    hovered = now;
    memo.invalidate();
    update();
}

void TableButtonItem::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
    setHovered(isEnabled());
}

void TableButtonItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    setHovered(false);
}

// TAKEN, so the release comes here - and, as SeatItem's, so a press on the button
// never reaches whatever lies beneath it.
void TableButtonItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

// A click is a press and a release on the button: let go elsewhere, and it is not.
void TableButtonItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if(isEnabled() && event->button() == Qt::LeftButton && shape().contains(event->pos()))
        emit clicked();
}

QVariant TableButtonItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if(change == ItemEnabledHasChanged)
    {
        // A disabled button takes neither the cursor nor a click, and shows it: a
        // control that looks live and does nothing is a defect of its own.
        setAcceptHoverEvents(value.toBool());
        setAcceptedMouseButtons(value.toBool() ? Qt::LeftButton : Qt::NoButton);

        if(!value.toBool())
            hovered = false;

        memo.invalidate();
        update();
    }

    return QGraphicsObject::itemChange(change, value);
}

void TableButtonItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const panel::Picture& made = picture(devicepixels::effectiveRatio(*painter));

    if(made.pixmap.isNull())
        return;

    // On a whole device pixel, as the picture's own pixels were laid out to be.
    painter->drawPixmap(devicepixels::snapped(*painter, made.offset), made.pixmap);
}

const panel::Picture& TableButtonItem::picture(qreal ratio) const
{
    return memo.picture(ratio, [&]() -> panel::Picture
    {
        const panel::State state = hovered ? panel::State::Hovered : panel::State::Plain;
        const panel::Picture frame = panels->picture(buttonSize, theme.table, state, ratio);

        if(frame.pixmap.isNull())
            return {};

        panel::Picture composed;
        composed.pixmap = QPixmap(frame.pixmap.size());
        composed.pixmap.setDevicePixelRatio(ratio);
        composed.pixmap.fill(Qt::transparent);
        composed.offset = frame.offset;

        QPainter painter(&composed.pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawPixmap(QPointF(0.0, 0.0), frame.pixmap);

        // The button's own units from here, its top-left at the origin.
        painter.translate(-frame.offset);

        // .btn: the icon in gold and the label in cream; both gold-bright under the
        // cursor; and, disabled, both muted.
        const Theme::TablePalette& palette = theme.table;

        QColor iconColour = palette.gold;
        QColor labelColour = palette.cream;

        if(!isEnabled())
            iconColour = labelColour = palette.muted;
        else if(hovered)
            iconColour = labelColour = palette.goldBright;

        // align-items: center, so the icon and the label's line box are both
        // centred on the button's height.
        const qreal left = borderWidth + paddingX;

        icons::paint(painter, icon,
                     QRectF(left, (tablebutton::height - iconSize) / 2.0, iconSize, iconSize),
                     iconColour);

        const QFont font = labelFont();
        const QFontMetricsF metrics(font);
        const qreal line = metrics.lineSpacing();

        painter.setFont(font);
        painter.setPen(labelColour);
        painter.drawText(QPointF(left + iconSize + iconGap,
                                 fonts::baselineIn(metrics, (tablebutton::height - line) / 2.0, line)),
                         text);

        painter.end();

        return composed;
    });
}
