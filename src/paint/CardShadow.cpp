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

// page.html's .tcard .inner, in table units: 0 6px 14px, and the winning card's
// 0 16px 24px under a 0 0 26px glow and a 2px ring.
constexpr QPointF plainOffset(0.0, 6.0);
constexpr qreal plainBlur = 14.0;

constexpr QPointF winningOffset(0.0, 16.0);
constexpr qreal winningBlur = 24.0;
constexpr qreal glowBlur = 26.0;
constexpr qreal ringWidth = 2.0;

// The card's rounded corners, which the shadow and the ring follow: .tcard .inner's
// border-radius, which also clips the art.
constexpr qreal cornerRadius = 7.0;

// The card centred on the origin, as CardItem draws it.
QRectF cardRect(QSizeF size)
{
    return QRectF(QPointF(-size.width() / 2.0, -size.height() / 2.0), size);
}

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

}

QMarginsF cardshadow::reach(bool winning)
{
    // A unit more for the anti-aliased edge, as the racks have.
    const QMarginsF edge(1.0, 1.0, 1.0, 1.0);

    if(!winning)
        return shadowReach(plainOffset, plainBlur) + edge;

    const QMarginsF ring(ringWidth, ringWidth, ringWidth, ringWidth);

    return widest(widest(shadowReach(winningOffset, winningBlur), shadowReach(QPointF(), glowBlur)), ring)
           + edge;
}

cardshadow::Colours cardshadow::colours(const Theme::TablePalette& palette)
{
    return { palette.playedShadow, palette.winningShadow, palette.winningGlow, palette.goldBright };
}

cardshadow::Picture cardshadow::render(QSizeF size, bool winning, const Colours& colours, qreal ratio)
{
    if(size.isEmpty() || ratio <= 0.0)
        return {};

    const QRectF card = cardRect(size);

    // THE CORNER A WHOLE NUMBER OF PIXELS FROM THE CARD'S, not from the origin. The
    // card's art is a picture of its own, laid at the card's corner; CardItem snaps
    // that corner to a device pixel and lays this picture the same whole pixels
    // from it, so the shadow's cut-out and the ring land on the art's own edge
    // rather than a pixel either side of it.
    devicepixels::Canvas canvas = devicepixels::canvas(card.marginsAdded(reach(winning)), ratio, card.topLeft());

    if(canvas.pixmap.isNull())
        return {};

    QPainter painter(&canvas.pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-canvas.offset);

    // OUTSIDE THE CARD ONLY, which drawShapeShadow() does on its own: CSS clips an
    // outer box-shadow to outside the element, so where the art's own corners are
    // rounder than the clip, the felt shows there and not a shadow.
    const QPainterPath body = paths::rounded(card, cornerRadius);

    if(!winning)
    {
        shadows::drawShapeShadow(painter, body, plainOffset, plainBlur, colours.shadow);
    }
    else
    {
        // Bottom to top: the deeper shadow, the glow, then the ring.
        shadows::drawShapeShadow(painter, body, winningOffset, winningBlur, colours.winningShadow);
        shadows::drawShapeShadow(painter, body, QPointF(), glowBlur, colours.winningGlow);

        // THE RING FILLED THROUGH, UNDER THE CARD - shadows::Side::Beneath's idea,
        // for the same reason. Cut to a band, its inner edge and the art's edge would
        // each half-cover the pixels they share, and the felt would show between
        // the gold and the card as a dark hairline. Filled whole, the art meets one
        // edge, and where its corners are rounder than this one the gold shows in
        // them rather than the felt.
        painter.fillPath(paths::rounded(card.adjusted(-ringWidth, -ringWidth, ringWidth, ringWidth),
                                        cornerRadius + ringWidth),
                         colours.ring);
    }

    painter.end();

    return { canvas.pixmap, canvas.offset };
}

cardshadow::Picture cardshadow::Cache::picture(QSizeF size, bool winning, const Colours& wanted, qreal ratio)
{
    if(size.isEmpty() || ratio <= 0.0)
        return {};

    return pictures.picture({ size, winning }, wanted, ratio,
                            [&] { return render(size, winning, wanted, ratio); });
}
