#include "LeadMarkItem.h"

#include "paint/DevicePixels.h"
#include "paint/Shadows.h"
#include "table/TableSpace.h"
#include "table/Turn.h"

#include <QPainter>
#include <QPainterPath>

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

LeadMarkItem::LeadMarkItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    // Nothing beneath it can be clicked, and neither can it.
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(tablespace::z::leadMark);
    hide();
}

void LeadMarkItem::setMark(std::optional<tablespace::Placement> place, const Theme::TablePalette& colours)
{
    if(colours != palette)
    {
        palette = colours;
        memo.invalidate();
        update();
    }

    if(!place)
    {
        hide();
        return;
    }

    setPos(place->centre);
    turnTo(*this, place->rotation);
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

    const devicepixels::Canvas& picture = memo.picture(pictureAt, [&]
    {
        devicepixels::Canvas made = devicepixels::canvas(
            QRectF(-pictureReach, -pictureReach, 2.0 * pictureReach, 2.0 * pictureReach), pictureAt);

        if(made.pixmap.isNull())
            return made;

        QPainter draw(&made.pixmap);
        draw.setRenderHint(QPainter::Antialiasing);
        draw.translate(-made.offset);

        // CSS's box-shadows, the first listed on top - the outline over the glow -
        // both outside the square only, and the square over them.
        const QPainterPath square = diamond(side);

        shadows::drawShapeShadow(draw, square, QPointF(), glowBlur, palette.leadMarkGlow);

        QPainterPath ring = diamond(side + 2.0 * outline);
        ring.addPath(square);
        draw.fillPath(ring, palette.leadMarkOutline);

        draw.fillPath(square, palette.goldBright);
        draw.end();

        return made;
    });

    if(picture.pixmap.isNull())
        return;

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->scale(1.0, stretch);
    painter->drawPixmap(picture.offset, picture.pixmap);
    painter->restore();
}
