#include "CardShadow.h"

#include "paint/DevicePixels.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"

#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace
{

using cardshadow::Look;

// ONE LOOK'S BOX-SHADOWS, in table units: a shadow, and for the winner and the
// raised hand card a glow over it and a ring over that.
struct Cast
{
    QPointF offset;
    qreal blur = 0.0;

    // The glow's blur, centred on the card; none where it is zero.
    qreal glow = 0.0;
    bool ring = false;

    // The card's rounded corners, which the shadow and the ring follow: the
    // element's border-radius, which also clips the art.
    qreal corner = 0.0;
};

// page.html. .tcard .inner: 0 6px 14px, radius 7; the winner's 0 16px 24px under a
// 0 0 26px glow and a 2px ring. .hcard .face: -3px 4px 10px, radius 8; raised under
// the cursor, 0 14px 22px under a 0 0 22px glow and the same ring.
Cast castOf(Look look)
{
    switch(look)
    {
    case Look::Played:  return { QPointF(0.0, 6.0), 14.0, 0.0, false, 7.0 };
    case Look::Winning: return { QPointF(0.0, 16.0), 24.0, 26.0, true, 7.0 };
    case Look::Held:    return { QPointF(-3.0, 4.0), 10.0, 0.0, false, 8.0 };
    case Look::Raised:  return { QPointF(0.0, 14.0), 22.0, 22.0, true, 8.0 };
    }

    return {};
}

constexpr qreal ringWidth = 2.0;

// How far a shadow moved by `offset` and blurred by `blur` reaches past the card,
// on each side.
QMarginsF shadowReach(QPointF offset, qreal blur)
{
    const qreal reach = shadows::reach(blur);

    return QMarginsF(std::max(0.0, reach - offset.x()), std::max(0.0, reach - offset.y()),
                     std::max(0.0, reach + offset.x()), std::max(0.0, reach + offset.y()));
}

QMarginsF widest(QMarginsF a, QMarginsF b)
{
    return QMarginsF(std::max(a.left(), b.left()), std::max(a.top(), b.top()),
                     std::max(a.right(), b.right()), std::max(a.bottom(), b.bottom()));
}

// Each look's shadow and glow colours.
QColor shadowColour(Look look, const cardshadow::Colours& colours)
{
    switch(look)
    {
    case Look::Played:  return colours.shadow;
    case Look::Winning: return colours.winningShadow;
    case Look::Held:    return colours.heldShadow;
    case Look::Raised:  return colours.raisedShadow;
    }

    return {};
}

QColor glowColour(Look look, const cardshadow::Colours& colours)
{
    return look == Look::Raised ? colours.raisedGlow : colours.winningGlow;
}

}

QRectF cardshadow::cardRect(QSizeF size)
{
    return QRectF(QPointF(-size.width() / 2.0, -size.height() / 2.0), size);
}

QMarginsF cardshadow::reach(Look look)
{
    const Cast cast = castOf(look);

    QMarginsF reach = shadowReach(cast.offset, cast.blur);

    if(cast.glow > 0.0)
        reach = widest(reach, shadowReach(QPointF(), cast.glow));

    if(cast.ring)
        reach = widest(reach, QMarginsF(ringWidth, ringWidth, ringWidth, ringWidth));

    // A unit more for the anti-aliased edge, as the racks have.
    return reach + QMarginsF(1.0, 1.0, 1.0, 1.0);
}

cardshadow::Colours cardshadow::colours(const Theme::TablePalette& palette)
{
    return { palette.playedShadow, palette.winningShadow, palette.winningGlow, palette.goldBright,
             palette.heldShadow, palette.raisedShadow, palette.raisedGlow };
}

cardshadow::Picture cardshadow::render(QSizeF size, Look look, const Colours& colours, qreal ratio)
{
    if(size.isEmpty() || ratio <= 0.0)
        return {};

    const QRectF card = cardRect(size);
    const Cast cast = castOf(look);

    // THE CORNER A WHOLE NUMBER OF PIXELS FROM THE CARD'S, not from the origin. The
    // card's art is a picture of its own, laid at the card's corner; CardItem snaps
    // that corner to a device pixel and lays this picture the same whole pixels
    // from it, so the shadow's cut-out and the ring land on the art's own edge
    // rather than a pixel either side of it.
    devicepixels::Canvas canvas = devicepixels::canvas(card.marginsAdded(reach(look)), ratio, card.topLeft());

    if(canvas.pixmap.isNull())
        return {};

    QPainter painter(&canvas.pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-canvas.offset);

    // OUTSIDE THE CARD ONLY, which drawShapeShadow() does on its own: CSS clips an
    // outer box-shadow to outside the element, so where the art's own corners are
    // rounder than the clip, the felt shows there and not a shadow.
    const QPainterPath body = paths::rounded(card, cast.corner);

    // Bottom to top: the shadow, the glow, then the ring.
    shadows::drawShapeShadow(painter, body, cast.offset, cast.blur, shadowColour(look, colours));

    if(cast.glow > 0.0)
        shadows::drawShapeShadow(painter, body, QPointF(), cast.glow, glowColour(look, colours));

    if(cast.ring)
    {
        // THE RING FILLED THROUGH, UNDER THE CARD - shadows::Side::Beneath's idea,
        // for the same reason. Cut to a band, its inner edge and the art's edge would
        // each half-cover the pixels they share, and the felt would show between
        // the gold and the card as a dark hairline. Filled whole, the art meets one
        // edge, and where its corners are rounder than this one the gold shows in
        // them rather than the felt.
        painter.fillPath(paths::rounded(card.adjusted(-ringWidth, -ringWidth, ringWidth, ringWidth),
                                        cast.corner + ringWidth),
                         colours.ring);
    }

    painter.end();

    return { canvas.pixmap, canvas.offset };
}

cardshadow::Picture cardshadow::Cache::picture(QSizeF size, Look look, const Colours& wanted, qreal ratio)
{
    if(size.isEmpty() || ratio <= 0.0)
        return {};

    return pictures.picture({ size, look }, wanted, ratio,
                            [&] { return render(size, look, wanted, ratio); });
}
