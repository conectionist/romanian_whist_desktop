#include "LeadMarkItem.h"

#include "paint/DevicePixels.h"
#include "paint/Shadows.h"
#include "table/TableSpace.h"

#include <QPainter>
#include <QTransform>

#include <cmath>
#include <numbers>

namespace
{

// .leadpip: 11 x 11, with a 1px spread outline and a 10px glow, and the transform
// rotate(a - 90deg) scaleY(1.45) rotate(45deg).
constexpr qreal side = 11.0;
constexpr qreal outline = 1.0;
constexpr qreal glowBlur = 10.0;
constexpr qreal stretch = 1.45;

// A square of `size` turned 45 degrees about the origin: a diamond whose corners
// lie on the axes, half its diagonal from the middle.
QPainterPath diamond(qreal size)
{
    const qreal half = size / std::numbers::sqrt2;

    QPainterPath path;
    path.moveTo(0.0, -half);
    path.lineTo(half, 0.0);
    path.lineTo(0.0, half);
    path.lineTo(-half, 0.0);
    path.closeSubpath();
    return path;
}

// How far the picture reaches from the middle, before the stretch: the outline's
// corner, the glow's reach past the square's, and a unit for the anti-aliased edge.
constexpr qreal pictureReach = (side + 2.0 * outline) / std::numbers::sqrt2 + shadows::reach(glowBlur) + 1.0;

}

QPainterPath leadmark::shape()
{
    return QTransform::fromScale(1.0, stretch).map(diamond(side));
}

LeadMarkItem::LeadMarkItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    // Nothing beneath it can be clicked, and neither can it.
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(tablespace::z::leadMark);
    hide();
}

void LeadMarkItem::setMark(std::optional<trickitem::Placement> place, const Theme::TablePalette& colours)
{
    if(colours != palette)
    {
        palette = colours;
        pictureRatio = 0;
        update();
    }

    if(!place)
    {
        hide();
        return;
    }

    setPos(place->centre);
    setRotation(place->rotation);
    show();
}

QRectF LeadMarkItem::boundingRect() const
{
    return QRectF(-pictureReach, -pictureReach * stretch, 2.0 * pictureReach, 2.0 * pictureReach * stretch);
}

void LeadMarkItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const qreal ratio = devicepixels::effectiveRatio(*painter);

    if(ratio <= 0.0)
        return;

    // At the pixels the stretch will spread it over, so its long axis stays sharp.
    const qreal pictureAt = ratio * stretch;
    const int key = devicepixels::ratioKey(pictureAt);

    if(key != pictureRatio || picture.isNull())
    {
        const QPointF corner(std::floor(-pictureReach * pictureAt) / pictureAt,
                             std::floor(-pictureReach * pictureAt) / pictureAt);
        const int pixels = static_cast<int>(std::ceil((pictureReach - corner.x()) * pictureAt));

        QPixmap made(pixels, pixels);
        made.setDevicePixelRatio(pictureAt);
        made.fill(Qt::transparent);

        QPainter draw(&made);
        draw.setRenderHint(QPainter::Antialiasing);
        draw.translate(-corner);

        // CSS's box-shadows, the first listed on top - the outline over the glow -
        // both outside the square only, and the square over them.
        const QPainterPath square = diamond(side);

        shadows::drawShapeShadow(draw, square, QPointF(), glowBlur, palette.leadMarkGlow);

        QPainterPath ring = diamond(side + 2.0 * outline);
        ring.addPath(square);
        draw.fillPath(ring, palette.leadMarkOutline);

        draw.fillPath(square, palette.goldBright);
        draw.end();

        picture = made;
        offset = corner;
        pictureRatio = key;
    }

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->scale(1.0, stretch);
    painter->drawPixmap(offset, picture);
    painter->restore();
}
