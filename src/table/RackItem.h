#ifndef RACK_ITEM_H
#define RACK_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QGraphicsObject>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

#include <cstddef>
#include <vector>

class CardRenderer;

// WHAT GOES WHERE IN A RACK, which is the part of it with a right answer - so free
// functions, the split seatitem and seatlayout follow, checked with no scene and no
// painter.
//
// THE RACK'S OWN FRAME, as table.js lays out a .fan: the origin on the rim, +x
// along the rim, and +y OUTWARD, toward the rack's owner. RackItem is rotated into
// place by frameRotation(), so nothing here knows which seat it is.
//
// A CARD STANDS IN THE RACK TILTED, and that is the only 3D on the table. The
// mock-up stands each card 50 degrees up out of the table about its top edge and
// looks at it through a perspective of 300 (page.html's .fan and .fan .back).
// The port keeps the geometry and not the mechanism: each card is a flat picture
// drawn through the quad it projects to, which QTransform::quadToQuad() can map
// exactly. No OpenGL.
namespace rackitem
{

// The most cards a hand holds. The bounding rect is sized for a full rack, so a
// card leaving changes nothing the scene has indexed.
inline constexpr std::size_t mostCards = 8;

// A card's size in the rack before it is tilted: tablespace::rackCardHeight, and
// the mock-up's 62 wide - which CardRenderer::sizeForHeight() gives too, and a
// test holds them together.
QSizeF cardSize();

// THE FOUR CORNERS CARD `index` OF `count` PROJECTS TO, in the rack's frame, in
// the order top-left, top-right, bottom-right, bottom-left of the flat card - so
// its top edge is the one at the rail and its bottom edge the far one, which is
// wider: it has risen toward the viewer.
//
// IN CSS'S ORDER. Each .back is translateX(t) rotate(theta) rotateX(50deg) about
// its own top-centre, and the .fan's perspective projects the result; so the
// corners are tilted, then turned, then moved along the row, and only then
// projected - which spreads the outer cards' far ends wider than projecting each
// card first would.
QPolygonF cardQuad(std::size_t count, std::size_t index);

// The flat card, QRectF(QPointF(), cardSize()), onto cardQuad(). What a card in
// the rack is painted through.
QTransform cardTransform(std::size_t count, std::size_t index);

// How much a card's far edge is magnified by the perspective, so its picture can
// be made at that many more pixels and stay sharp where it is largest.
qreal farMagnification();

// The rail the cards stand in, lying on the rim across their near edges: the
// row's width plus 16, from 9 toward the table to 7 past the cards' top edge, and
// raised 8 toward the viewer, which magnifies it a little. Empty with no cards.
QRectF railRect(std::size_t count);

// Everything a rack of `count` draws, shadows aside: the cards and the rail.
QRectF extent(std::size_t count);

// The item's rotation, in DEGREES as QGraphicsItem::setRotation() takes it, for a
// rack at `angle` (radians, y down, as seatlayout::rackAngle() gives it): the
// frame turned so its +y points from the table's centre out through the rack.
qreal frameRotation(qreal angle);

// WHAT A RACK SHOWS, worked out once per snapshot. The picture is composed from
// this and composed again only when it changes, so anything drawn in the rack has
// to be in here.
struct Shown
{
    // How many cards stand in the rack. None draws nothing, not even the rail.
    std::size_t count = 0;

    // The cards face up, left to right in the rack's frame, when the seat's hand
    // can be seen - a Forehead round. Empty otherwise: every card is a back.
    std::vector<romanian_whist::Card> faces;

    bool operator==(const Shown&) const = default;
};

// A seat's rack. NOT "is this a Forehead round": the snapshot fills a seat's hand
// only where canSeeHand(viewer, holder) allows, so a non-empty hand IS the
// permission - and a spectated game fills none, so it shows backs throughout.
// Otherwise one back per card left.
Shown shownFor(const SeatView& seat);

// A RACK AS A PICTURE, in its own frame: the shadow, the cards and the rail, made
// at `ratio`. `offset` is where its top-left goes in the rack's frame, on a whole
// pixel at that ratio. Null for a rack with no cards.
struct Picture
{
    QPixmap pixmap;
    QPointF offset;
};

Picture render(const Shown& shown, const Theme::TablePalette& palette, qreal ratio,
               const CardRenderer& renderer);

// render()'S PICTURES, SHARED by every rack showing the same cards. A rack is drawn
// in its own frame and turned into place by its item, so five opponents holding
// five cards each are five copies of one picture - and composing each of them
// again on every settle of a resize was 8 ms the redesign's phase 5 measured.
//
// ONE RATIO AND ONE PALETTE AT A TIME, as panel::Cache is: a new ratio means the
// window was resized or moved, and the old pictures are never asked for again.
//
// Owned by whatever owns the items (TableScene), which outlives them.
class Cache
{
public:
    // BY VALUE, which costs a reference count: a reference into the cache would
    // dangle the moment another rack's request cleared it.
    Picture picture(const Shown& shown, const Theme::TablePalette& palette, qreal ratio,
                    const CardRenderer& renderer);

    // How many pictures this has rendered: READ-ONLY and for tests.
    std::size_t renders() const { return rendered; }

private:
    struct Entry
    {
        Shown shown;
        Picture picture;
    };

    int ratioKey = 0;
    Theme::TablePalette palette;
    std::vector<Entry> entries;
    std::size_t rendered = 0;
};

} // namespace rackitem

// One opponent's hand, standing in a wooden rack on the rim (the table redesign's
// section 2.6), and the count of cards they have left that the name panel used to
// carry.
//
// A QGraphicsObject for the reason SeatItem and CardItem are: a card leaving the
// rack for the table is section 18.2's to animate.
//
// ONE CACHED PICTURE, as a name panel is. The shadow is a blur and every card is
// drawn through a projective transform, too dear for every frame of a window being
// dragged; so the rack is composed once per thing it shows and per ratio, and
// blitted - and shared through rackitem::Cache with every rack that shows the same.
// The picture is in the rack's own frame, and the item's rotation turns it into
// place.
class RackItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // The renderer outlives every item, as CardItem documents: TableScene owns it
    // by value and the items are its children. `racks` is the picture cache every
    // rack shares, owned with the renderer; null gives the item one of its own.
    RackItem(const CardRenderer* renderer, rackitem::Cache* racks, QGraphicsItem* parent = nullptr);

    // Everything this draws: the seat's cards, and the table's colours for the
    // rail and the light on them.
    void setRack(const SeatView& seat, const Theme& theme);

    // What it shows. READ-ONLY and for tests.
    const rackitem::Shown& shown() const { return showing; }

    // A full rack plus its shadow, whatever it holds now - see rackitem::mostCards.
    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // The picture paint() blits at `ratio`, composed if it is not already held.
    // Public because it is the contract a test can check, as TableTopItem's is.
    QPixmap picture(qreal ratio) const;

    // How many times this rack has asked for a new picture: READ-ONLY and for tests,
    // which show with it that a resize settling leaves it alone - SeatItem's twin.
    // The cache may have had it already; rackitem::Cache::renders() counts those
    // that were composed.
    std::size_t compositions() const { return composedCount; }

private:
    const CardRenderer* renderer;

    mutable rackitem::Cache ownRacks;
    rackitem::Cache* racks;

    rackitem::Shown showing;
    Theme::TablePalette palette;

    // The rack's picture. The ratio is compared on its own, because it changes
    // without any setter.
    mutable rackitem::Picture composed;
    mutable int composedRatio = 0;
    mutable bool stale = true;
    mutable std::size_t composedCount = 0;
};

#endif
