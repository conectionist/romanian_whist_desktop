#ifndef TABLE_SPACE_H
#define TABLE_SPACE_H

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <cstddef>

// THE TABLE'S OWN COORDINATES, and the only place their numbers are written.
//
// The table redesign (docs/ui-redesign/IMPLEMENTATION_PLAN.md, section 2.1) lays
// everything out in a fixed logical space and scales that space to the window,
// which is what its mock-up does. Every number in that document's section 2 is in
// these units - a panel at (628, -140), a card 225 tall, a felt of radius 440 -
// so a phase that copies one of them copies it verbatim, and no layout is
// recomputed when the window changes size. TableView scales; nothing else moves.
//
// IT REPLACES A LAYOUT THAT WORKED THE OTHER WAY ROUND. Until the redesign's
// phase 2 the scene WAS the viewport, a card height was derived from its size,
// and every position followed from that height. That is gone, and so is the
// question it answered: the card sizes below are the mock-up's, not a fit.
//
// A NAMESPACE OF constexprs rather than members of TableScene, so the items the
// later phases add can read them without reaching through the scene.
namespace tablespace
{

// The stage the mock-up draws on. Its centre is the table's.
inline constexpr QSizeF stage(1536.0, 1024.0);
inline constexpr QPointF centre(768.0, 512.0);

// WHAT IS FITTED TO THE WINDOW: the stage plus room round it for the name panels,
// which sit outside the table - the top seat's reaches y = -140, the human's
// y = 1164. Symmetric about the centre in both directions, so fitting the box and
// centring it is the same thing as centring the table. Section 2.1, table.js fit().
inline constexpr QRectF fitBox(-172.0, -150.0, 1880.0, 1324.0);

// THE GAP LEFT ROUND THE FIT BOX, in device-independent pixels rather than table
// units: the mock-up's #viewport padding, which fit() fits inside. 16 at each side,
// 10 under the menu bar (its padding-top of 56 less the 46-pixel bar) and 8 at the
// bottom. They are also what keeps the scaled box strictly smaller than the
// viewport, so a QGraphicsView never sees a scroll range to round.
inline constexpr qreal marginX = 16.0;
inline constexpr qreal marginTop = 10.0;
inline constexpr qreal marginBottom = 8.0;

// The felt's radius (section 2.4). Played cards are placed relative to its edge.
inline constexpr qreal feltRadius = 440.0;

// CARD HEIGHTS, from section 2. Only the heights: the widths follow from the art
// through CardRenderer::sizeForHeight(), which is the one place the aspect is known
// (225 tall comes back 155 wide, the mock-up's 154.9).
inline constexpr qreal handCardHeight = 225.0;
inline constexpr qreal rackCardHeight = 90.0;

// Where the top of the human's hand is (section 2.8). Their played card sits 60
// above it.
inline constexpr qreal handTop = 850.0;

// The scale that fits the box, margins and all, into a viewport of this size -
// table.js fit()'s `scale`. Zero when the viewport has no room left inside its
// margins, which a caller reads as "leave the transform alone".
qreal fitScale(QSizeF viewport);

// A played card's height (section 2.7): the larger size up to four seats, the
// smaller from five, where six cards have to share the felt.
qreal playedCardHeight(std::size_t seatCount);

} // namespace tablespace

#endif
