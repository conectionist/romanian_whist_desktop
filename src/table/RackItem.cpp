#include "RackItem.h"

#include "paint/CardRenderer.h"
#include "paint/DevicePixels.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"
#include "table/TableSpace.h"

#include <QBrush>
#include <QLinearGradient>
#include <QMarginsF>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{

// page.html's .fan and .fan .back, and table.js renderFans(), in table units.
//
// The row: each card 26 along from the last, and turned 2 degrees the other way
// about its own top-centre, so the row fans open toward its owner.
constexpr qreal cardSpacing = 26.0;
constexpr qreal fanStep = -2.0 * std::numbers::pi / 180.0;

// A card's top edge sits 4 past the rack's origin, and the card stands 50 degrees
// up out of the table about that edge.
constexpr qreal cardTop = 4.0;
constexpr qreal tilt = 50.0 * std::numbers::pi / 180.0;

// The camera: perspective: 300px, perspective-origin: 0 40px.
constexpr qreal perspective = 300.0;
constexpr qreal perspectiveOriginY = 40.0;

// The cards' rounded corners, and the hairline round each one's edge.
constexpr qreal cardRadius = 4.0;
constexpr qreal cardEdge = 1.0;

// .fan .ledge: 16 wider than the row (78 - 62), from 9 toward the table to 7 past
// the cards' top edge, radius 3, raised 8 toward the viewer by translateZ(8px).
constexpr qreal railOverhang = 16.0;
constexpr qreal railTop = -9.0;
constexpr qreal railHeight = 16.0;
constexpr qreal railRadius = 3.0;
constexpr qreal railRaise = 8.0;
constexpr qreal railEdge = 1.0;

// The shadows: the row's drop-shadow(0 5px 5px), and the rail's own box-shadow,
// 0 2px 3px - both toward the owner, in the rack's frame.
constexpr QPointF rowShadowOffset(0.0, 5.0);
constexpr qreal rowShadowBlur = 5.0;
constexpr QPointF railShadowOffset(0.0, 2.0);
constexpr qreal railShadowBlur = 3.0;

// HOW FAR PAST extent() THE PICTURE REACHES: the row's shadow, which is the larger
// of the two, where its offset carries it - so further on the owner's side than on
// the table's - and a unit more for the anti-aliased edge. No further: every
// picture's transparent margin is blended on every frame of a drag, turned.
constexpr QMarginsF shadowMargins(shadows::reach(rowShadowBlur) + 1.0,
                                  shadows::reach(rowShadowBlur) - rowShadowOffset.y() + 1.0,
                                  shadows::reach(rowShadowBlur) + 1.0,
                                  shadows::reach(rowShadowBlur) + rowShadowOffset.y() + 1.0);

// A point in the .fan's own 3D space, through its perspective. z is toward the
// viewer, as CSS's is, so a point raised off the table is magnified about the
// perspective's origin.
QPointF project(qreal x, qreal y, qreal z)
{
    const qreal k = perspective / (perspective - z);

    return QPointF(x * k, perspectiveOriginY + (y - perspectiveOriginY) * k);
}

// A RackItem's rect for a hand of `count`: a full rack's at least, see mostCards.
QRectF rackBounds(std::size_t count)
{
    return rackitem::extent(std::max(count, rackitem::mostCards)).marginsAdded(shadowMargins);
}

}

QSizeF rackitem::cardSize()
{
    return QSizeF(62.0, tablespace::rackCardHeight);
}

QPolygonF rackitem::cardQuad(std::size_t count, std::size_t index)
{
    if(index >= count)
        return QPolygonF();

    const qreal along = static_cast<qreal>(index) - (static_cast<qreal>(count) - 1.0) / 2.0;
    const qreal shift = along * cardSpacing;
    const qreal turn = along * fanStep;

    const QSizeF size = cardSize();
    const qreal half = size.width() / 2.0;

    // The flat card's corners about its top-centre, which is the transform origin:
    // top-left, top-right, bottom-right, bottom-left.
    const QPointF flat[] = { { -half, 0.0 }, { half, 0.0 }, { half, size.height() }, { -half, size.height() } };

    QPolygonF quad;

    for(const QPointF& corner : flat)
    {
        // rotateX(50deg): the card's length stands up out of the table.
        const qreal down = corner.y() * std::cos(tilt);
        const qreal up = corner.y() * std::sin(tilt);

        // rotate(theta), in the table's plane.
        const qreal x = corner.x() * std::cos(turn) - down * std::sin(turn);
        const qreal y = corner.x() * std::sin(turn) + down * std::cos(turn);

        // translateX(t), back to the .fan's origin, and only then the perspective.
        quad << project(x + shift, y + cardTop, up);
    }

    return quad;
}

QTransform rackitem::cardTransform(std::size_t count, std::size_t index)
{
    const QPolygonF quad = cardQuad(count, index);

    if(quad.size() != 4)
        return QTransform();

    const QSizeF size = cardSize();
    const QPolygonF flat({ QPointF(0.0, 0.0), QPointF(size.width(), 0.0),
                           QPointF(size.width(), size.height()), QPointF(0.0, size.height()) });

    QTransform transform;
    QTransform::quadToQuad(flat, quad, transform);

    return transform;
}

qreal rackitem::farMagnification()
{
    return perspective / (perspective - cardSize().height() * std::sin(tilt));
}

QRectF rackitem::railRect(std::size_t count)
{
    if(count == 0)
        return QRectF();

    const qreal width = (static_cast<qreal>(count) - 1.0) * cardSpacing + cardSize().width() + railOverhang;

    return QRectF(project(-width / 2.0, railTop, railRaise),
                  project(width / 2.0, railTop + railHeight, railRaise));
}

QRectF rackitem::extent(std::size_t count)
{
    QRectF all = railRect(count);

    for(std::size_t i = 0 ; i < count ; i++)
        all = all.united(cardQuad(count, i).boundingRect());

    return all;
}

qreal rackitem::frameRotation(qreal angle)
{
    // The frame's +y is at a quarter turn past its +x, so the frame is turned a
    // quarter turn short of the rack's angle.
    qreal turn = std::remainder(qRadiansToDegrees(angle) - 90.0, 360.0);

    // (-180, 180]: QTransform::rotate() turns 180 exactly and -180 not.
    if(turn <= -180.0)
        turn += 360.0;

    // AND A QUARTER TURN EXACTLY, not the 89.99999999999999 a right angle can come
    // back from radians as, so the four-player racks take QTransform's exact path -
    // and so sit square on the pixel grid paint() snaps them to.
    const qreal quarters = std::round(turn / 90.0) * 90.0;

    return std::abs(turn - quarters) < 1e-9 ? quarters : turn;
}

rackitem::Shown rackitem::shownFor(const SeatView& seat)
{
    Shown shown;

    if(!seat.hand.empty())
    {
        for(const HandEntry& entry : seat.hand)
            shown.faces.push_back(entry.card);

        shown.count = shown.faces.size();
        return shown;
    }

    shown.count = seat.cardsLeft;
    return shown;
}

rackitem::Picture rackitem::render(const Shown& shown, const Theme::TablePalette& palette, qreal ratio,
                                   const CardRenderer& renderer)
{
    const std::size_t count = shown.count;

    // NO CARDS, NOTHING: no rail either, as table.js draws none for an empty hand.
    if(count == 0 || ratio <= 0.0)
        return {};

    // The picture's corner on a whole pixel at this ratio, so the cards and the rail
    // are laid on its grid as they were drawn.
    devicepixels::Canvas canvas = devicepixels::canvas(extent(count).marginsAdded(shadowMargins), ratio);
    const QPointF corner = canvas.offset;

    if(canvas.pixmap.isNull())
        return {};

    QPainter painter(&canvas.pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(-corner);

    const QSizeF size = cardSize();
    const QRectF flat(QPointF(0.0, 0.0), size);
    const QPainterPath body = paths::rounded(flat, cardRadius);

    const qreal railScale = perspective / (perspective - railRaise);
    const QRectF rail = railRect(count);
    const QPainterPath railBody = paths::rounded(rail, railRadius * railScale);

    // EACH CARD'S ART, FIRST, so the shadow is cast by the cards that are drawn.
    //
    // Made at the pixels its FAR end covers, which the perspective magnifies, so
    // the art is sampled down along the card rather than up at its far edge.
    //
    // A face that will not load stands in the rack as a back: the card is there,
    // and a gap would say it was not. With no art at all the card is left out of
    // the shadow as well, rather than cut out of it as a hole.
    const QSize artSize = size.toSize();
    const qreal artRatio = ratio * farMagnification();

    std::vector<QPixmap> arts;
    arts.reserve(count);

    for(std::size_t i = 0 ; i < count ; i++)
    {
        QPixmap art = i < shown.faces.size() ? renderer.face(shown.faces[i], artSize, artRatio) : QPixmap();

        if(art.isNull())
            art = renderer.back(artSize, artRatio);

        arts.push_back(art);
    }

    // THE ROW'S SHADOW, under everything: the .fan's drop-shadow is of the whole row,
    // the cards and the rail as one silhouette.
    QPainterPath silhouette;

    for(std::size_t i = 0 ; i < count ; i++)
    {
        if(!arts[i].isNull())
            silhouette.addPath(cardTransform(count, i).map(body));
    }

    silhouette.addPath(railBody);
    silhouette.setFillRule(Qt::WindingFill);

    shadows::drawShapeShadow(painter, silhouette, rowShadowOffset, rowShadowBlur, palette.rackShadow);

    // EACH CARD, later ones over earlier, as the mock-up's DOM order stacks them.

    // .back's gradient: light at the far end, which is the flat card's bottom,
    // clear by 45% of the way, and dark at the rail.
    QLinearGradient light(QPointF(0.0, size.height()), QPointF(0.0, 0.0));
    light.setColorAt(0.0, palette.rackCardLight);
    light.setColorAt(0.45, Qt::transparent);
    light.setColorAt(1.0, palette.rackCardDark);

    const QPainterPath edge = paths::roundedBand(flat, cardRadius,
                                                 flat.adjusted(cardEdge, cardEdge, -cardEdge, -cardEdge),
                                                 cardRadius - cardEdge);

    for(std::size_t i = 0 ; i < count ; i++)
    {
        QPixmap& art = arts[i];

        if(art.isNull())
            continue;

        // A BRUSH OVER THE CARD'S ROUNDED RECT, not a blit: a fill keeps the edge
        // anti-aliased under the projective transform, and a clip would not.
        art.setDevicePixelRatio(1.0);

        QBrush brush(art);
        brush.setTransform(QTransform::fromScale(size.width() / art.width(), size.height() / art.height()));

        painter.save();
        painter.setTransform(cardTransform(count, i), true);
        painter.fillPath(body, brush);
        painter.fillPath(body, light);
        painter.fillPath(edge, palette.rackCardEdge);
        painter.restore();
    }

    // THE RAIL, OVER THE CARDS' NEAR EDGES: raised 8 toward the viewer, so it is in
    // front of them, and its own shadow falls on them.
    shadows::drawShapeShadow(painter, railBody, railShadowOffset * railScale, railShadowBlur * railScale,
                             palette.railShadow);

    painter.fillPath(railBody, palette.railBase);

    QLinearGradient shade(rail.topLeft(), rail.bottomLeft());
    shade.setColorAt(0.0, palette.railLight);
    shade.setColorAt(0.3, Qt::transparent);
    shade.setColorAt(1.0, palette.railDark);
    painter.fillPath(railBody, shade);

    const qreal inset = railEdge * railScale;
    painter.fillPath(paths::roundedBand(rail, railRadius * railScale,
                                        rail.adjusted(inset, inset, -inset, -inset),
                                        std::max(0.0, (railRadius - railEdge) * railScale)),
                     palette.railEdge);

    painter.end();

    return { canvas.pixmap, corner };
}

rackitem::Picture rackitem::Cache::picture(const Shown& shown, const Theme::TablePalette& wanted,
                                           qreal ratio, const CardRenderer& renderer)
{
    if(shown.count == 0 || ratio <= 0.0)
        return {};

    return pictures.picture(shown, wanted, ratio, [&] { return render(shown, wanted, ratio, renderer); });
}

RackItem::RackItem(const CardRenderer* _renderer, rackitem::Cache* _racks, QGraphicsItem* parent)
    : QGraphicsObject(parent), renderer(_renderer), racks(_racks ? _racks : &ownRacks),
      bounds(rackBounds(0))
{
    // NOTHING HERE TAKES THE MOUSE. A rack stands on the rim, where nothing beneath
    // it can be clicked either.
    setAcceptedMouseButtons(Qt::NoButton);
}

void RackItem::setRack(const SeatView& seat, const Theme& theme)
{
    const rackitem::Shown now = rackitem::shownFor(seat);

    if(now == showing && theme.table == palette)
        return;

    // Only a hand larger than a full rack - which no round deals - changes the rect.
    if(std::max(now.count, rackitem::mostCards) != std::max(showing.count, rackitem::mostCards))
    {
        prepareGeometryChange();
        bounds = rackBounds(now.count);
    }

    showing = now;
    palette = theme.table;
    stale = true;
    update();
}

QRectF RackItem::boundingRect() const
{
    return bounds;
}

void RackItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    // At the pixels it lands on - or, while a resize settles, at the scale it
    // began from, so the cached rack is drawn scaled rather than composed again.
    const QPixmap rack = picture(devicepixels::effectiveRatio(*painter));

    if(rack.isNull())
        return;

    // ON A WHOLE DEVICE PIXEL. The picture's corner is whole only in the rack's own
    // frame, and the rack's origin on the rim is not, so it is snapped where it
    // lands: a quarter-turned rack - four players' every one - then lies pixel on
    // pixel, and the filter samples each at its centre. Turned any other way it is
    // filtered after all, as every played card is.
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawPixmap(devicepixels::snapped(*painter, composed.offset), rack);
}

QPixmap RackItem::picture(qreal ratio) const
{
    if(ratio <= 0.0 || !renderer)
        return QPixmap();

    const int key = devicepixels::ratioKey(ratio);

    if(stale || composedRatio != key)
    {
        composed = racks->picture(showing, palette, ratio, *renderer);
        composedRatio = key;
        stale = false;
        composedCount++;
    }

    return composed.pixmap;
}
