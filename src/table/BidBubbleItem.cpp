#include "BidBubbleItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"
#include "table/TableSpace.h"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QVariantAnimation>

#include <algorithm>

namespace
{

// page.html's .bubble.
constexpr qreal fontSize = 19.0;
constexpr qreal padX = 14.0;
constexpr qreal padY = 4.0;
constexpr qreal above = 18.0;
constexpr qreal panelBorder = 1.0;
constexpr QPointF shadowOffset(0.0, 6.0);
constexpr qreal shadowBlur = 14.0;

// @keyframes bubble.
constexpr qreal inEnds = 0.12;
constexpr qreal outStarts = 0.80;
constexpr qreal riseFrom = 6.0;

QFont bubbleFont()
{
    QFont font = fonts::heading();
    font.setPixelSize(static_cast<int>(fontSize));
    return font;
}

// CSS's `ease-out`: cubic-bezier(0, 0, .58, 1).
qreal easeOut(qreal t)
{
    static const QEasingCurve curve = []
    {
        QEasingCurve made(QEasingCurve::BezierSpline);
        made.addCubicBezierSegment(QPointF(0.0, 0.0), QPointF(0.58, 1.0), QPointF(1.0, 1.0));
        return made;
    }();

    return curve.valueForProgress(std::clamp(t, 0.0, 1.0));
}

// How far the picture reaches past the pill: the shadow, below more than above.
QRectF pictureRect(const QSizeF& pill)
{
    const qreal reach = shadows::reach(shadowBlur) + 1.0;

    return QRectF(QPointF(), pill).adjusted(-reach, -reach, reach, reach + shadowOffset.y());
}

}

QString bidbubble::textFor(bool yours, unsigned int bet)
{
    return yours ? QCoreApplication::translate("BidBubbleItem", "You bid %1").arg(bet)
                 : QCoreApplication::translate("BidBubbleItem", "Bids %1").arg(bet);
}

QSizeF bidbubble::sizeFor(const QString& text)
{
    const QFontMetricsF metrics(bubbleFont());

    return QSizeF(metrics.horizontalAdvance(text) + 2.0 * padX, metrics.height() + 2.0 * padY);
}

QRectF bidbubble::rectOver(const QRectF& panel, const QString& text)
{
    const QSizeF size = sizeFor(text);

    return QRectF(QPointF(panel.center().x() - size.width() / 2.0, panel.top() + panelBorder - above), size);
}

bidbubble::Frame bidbubble::frameAt(qreal progress)
{
    if(progress <= inEnds)
    {
        const qreal t = easeOut(progress / inEnds);
        return { t, riseFrom * (1.0 - t) };
    }

    if(progress <= outStarts)
        return { 1.0, 0.0 };

    return { 1.0 - easeOut((progress - outStarts) / (1.0 - outStarts)), 0.0 };
}

BidBubbleItem::BidBubbleItem(QGraphicsItem* parent)
    : QGraphicsObject(parent)
{
    // pointer-events: none. It floats over the panel and takes nothing from it.
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setZValue(tablespace::z::bubbles);
    hide();

    timeline = new QVariantAnimation(this);
    timeline->setStartValue(0.0);
    timeline->setEndValue(1.0);

    connect(timeline, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { step(value.toReal()); });

    // Gone when its time is up.
    connect(timeline, &QAbstractAnimation::finished, this, [this] { hide(); });
}

void BidBubbleItem::say(const QString& text, const QRectF& panel, motion::Pace pace,
                        const Theme::TablePalette& colours)
{
    timeline->stop();

    if(text != words || colours != palette)
    {
        prepareGeometryChange();
        words = text;
        pill = bidbubble::sizeFor(text);
        palette = colours;
        memo.invalidate();
    }

    setPos(bidbubble::rectOver(panel, text).topLeft());

    still = !motion::animates(pace);
    timeline->setDuration(still ? motion::bubbleStillMs : motion::bubbleMs);

    step(0.0);
    show();
    timeline->start();
}

void BidBubbleItem::dismiss()
{
    timeline->stop();
    hide();
}

void BidBubbleItem::step(qreal progress)
{
    // HELD STILL at Off: there from the first moment to the last, and then gone.
    const bidbubble::Frame frame = still ? bidbubble::Frame{} : bidbubble::frameAt(progress);

    setOpacity(frame.opacity);
    setTransform(QTransform::fromTranslate(0.0, frame.rise));
}

QRectF BidBubbleItem::boundingRect() const
{
    return pill.isEmpty() ? QRectF() : pictureRect(pill);
}

void BidBubbleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const qreal ratio = devicepixels::effectiveRatio(*painter);

    if(ratio <= 0.0 || pill.isEmpty())
        return;

    const devicepixels::Canvas& picture = memo.picture(ratio, [&]
    {
        devicepixels::Canvas made = devicepixels::canvas(pictureRect(pill), ratio);

        if(made.pixmap.isNull())
            return made;

        QPainter draw(&made.pixmap);
        draw.setRenderHint(QPainter::Antialiasing);
        draw.setRenderHint(QPainter::TextAntialiasing);
        draw.translate(-made.offset);

        // border-radius: 99px, which CSS clamps to half the height: a pill.
        const QRectF body(QPointF(), pill);
        const QPainterPath shape = paths::rounded(body, pill.height() / 2.0);

        shadows::drawShapeShadow(draw, shape, shadowOffset, shadowBlur, palette.bubbleShadow);
        draw.fillPath(shape, palette.goldBright);

        const QFont font = bubbleFont();
        const QFontMetricsF metrics(font);

        draw.setFont(font);
        draw.setPen(palette.bubbleText);
        draw.drawText(QPointF(padX, fonts::baselineIn(metrics, padY, metrics.height())), words);
        draw.end();

        return made;
    });

    if(!picture.pixmap.isNull())
        painter->drawPixmap(devicepixels::snapped(*painter, picture.offset), picture.pixmap);
}
