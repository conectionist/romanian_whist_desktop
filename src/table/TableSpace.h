#ifndef TABLE_SPACE_H
#define TABLE_SPACE_H

#include "paint/Shadows.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
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

// THE SMALLEST SCALE THE TABLE IS DRAWN AT. Below it the panels' names fall under
// nine pixels (Marcellus 25, seatitem::nameSize) and the Taken/Bid pill under
// seven, which is past reading - and nothing else would stop it: the table is a
// fixed space the view scales, so a small window used to draw it as small as it
// liked. Decided by the owner on 2026-10-17 at 0.30, for the old plaque's larger
// names; raised to 0.36 on 2026-10-18 when phase 4's panels made them 25. The
// smallest window is then minimumViewport() and the menu bar above it: since
// phase 8 the table is the whole page, with nothing beside it or below it.
// TableSpaceTests pins the nine pixels.
inline constexpr qreal minimumScale = 0.36;

// THE TABLE (section 2.4), every circle centred on `centre`.
//
// The square a circle of `radius` about the centre sits in: what a texture is
// laid over, a gradient sized to, the table's picture bounded by.
constexpr QRectF squareAround(qreal radius)
{
    return QRectF(centre.x() - radius, centre.y() - radius, 2.0 * radius, 2.0 * radius);
}
//
// The wooden rim, a true circle seen from straight above. Its shadow falls evenly
// round it: the light is directly overhead, so there is no offset.
inline constexpr qreal woodRadius = 510.0;

// The felt. Played cards are placed relative to its edge.
inline constexpr qreal feltRadius = 440.0;

// The bead between the two: three rings just outside the felt, each given by its
// outer radius - so the first runs from the felt's edge at 440 to 449. The mock-up
// draws them as the felt's box-shadow spreads of 9, 11 and 13.
inline constexpr qreal beadInnerRadius = 449.0;
inline constexpr qreal beadMiddleRadius = 451.0;
inline constexpr qreal beadOuterRadius = 453.0;

// THE GOLD CIRCLE ON THE FELT, by its OUTER edge. The mock-up draws it as a CSS
// border on a box inset 24 into the felt, and a border lies inside its box - so
// the line runs from 414.5 to 416, centred 0.75 inside the radius section 2.4
// gives it. The lead rhombus sits on it.
inline constexpr qreal goldLineRadius = 416.0;
inline constexpr qreal goldLineWidth = 1.5;

// The logo plaque: the mock-up's 270 x 142 box, drawn at 0.9 of that about the
// table's centre, which is where it sits.
inline constexpr QSizeF logoBox(270.0, 142.0);
inline constexpr qreal logoScale = 0.9;

// The table's shadow: CSS's blur of 70 round the rim, which is a Gaussian of
// sigma 35 - and shadows::reach() is where it has faded to nothing, so the
// picture of the table reaches that far and no further.
inline constexpr qreal tableShadowBlur = 70.0;
inline constexpr qreal tableReach = woodRadius + shadows::reach(tableShadowBlur);

// THE HAND'S CARD HEIGHT, from section 2.8. Only the height: the width follows from
// the art through CardRenderer::sizeForHeight(), which is the one place the aspect
// is known (225 tall comes back 155 wide, the mock-up's 154.9). Played cards are
// playedCardHeight() below, and the racks' cards rackCardHeight.
inline constexpr qreal handCardHeight = 225.0;

// Where the top of the human's hand is (section 2.8). Their played card sits 60
// above it.
inline constexpr qreal handTop = 850.0;

// THE PLAYED CARDS (section 2.7). Each opponent's lies on the line from the centre
// to its rack with its outer end this far inside the felt's edge, and yours with
// its bottom edge this far above your hand. trickitem::cardPlacement() is where.
inline constexpr qreal playedInset = 60.0;

// THE LEAD RHOMBUS (section 2.7): an opponent's sits on the gold circle
// (goldLineRadius) in line with their rack. Yours would sit under your hand there,
// so it goes in the gap between your card and your hand, half way.
inline constexpr qreal leadMarkHumanY = handTop - playedInset / 2.0;

// THE NAME PANELS (section 2.5). Where each one goes is seatlayout::panelRect(),
// from a table hand-tuned per player count for the mock-up's panel of this size:
// every panel is centred where that one was, so the racks stay where they were.
inline constexpr QSizeF panelSpot(280.0, 118.0);

// EVERY PANEL'S SIZE, yours included: the portrait, and a text column as wide as
// the widest Taken/Bid pill and no wider, now the name has no score after it (the
// owner's experiment of 2026-10-21). SeatItemTests holds the column to the pill.
inline constexpr QSizeF panelSize(259.0, 118.0);

// THE RACKS (section 2.6): each opponent's hand, standing in a wooden rack on the
// rim. A rack's origin is on this circle, table.js's RIM_R - on the bead, between
// its middle ring at 451 and its outer one at 453, so the rack stands out over the
// wood toward the rim's edge at 510 - and its cards are 90 tall before they are
// tilted.
// Where on the circle is seatlayout::rackAngle().
inline constexpr qreal rackRadius = 452.0;
inline constexpr qreal rackCardHeight = 90.0;

// The bottom seat's panel: yours, or seat 0's in a spectated game. Centred on the
// table at (768, 1105), and it covers the bottom of your hand - which section 2.8
// intends. The mock-up's was 300 wide, for a longer name line than any other seat
// has room for; with no score on it, it is the others' size.
inline constexpr QRectF bottomPanel(768.0 - panelSize.width() / 2.0, 1046.0,
                                    panelSize.width(), panelSize.height());

// THE CORNER PANELS (sections 2.1 and 2.9 to 2.11): the scoreboard top-left, the
// trick panel top-right, the two buttons bottom-left. PINNED TO THE WINDOW, NOT TO
// TABLE SPACE - each this many device-independent pixels in from its corner of
// the viewport, whose top is already under the menu bar - and drawn at the
// table's scale, so they grow and shrink with it. TableScene::pinHud() places
// them; table.js fit() is where the mock-up does.
inline constexpr qreal hudInset = 8.0;

// The scoreboard and the trick panel are drawn this much larger than the table
// (page.html's transform: scale(1.2)); the buttons at the table's own scale.
inline constexpr qreal hudScale = 1.2;

// Between the two buttons: the mock-up puts Last trick 228 right of Round history,
// which is 214 wide.
inline constexpr qreal buttonGap = 14.0;

// The scale that fits the box, margins and all, into a viewport of this size -
// table.js fit()'s `scale`. Zero when the viewport has no room left inside its
// margins, which a caller reads as "leave the transform alone".
qreal fitScale(QSizeF viewport);

// The smallest viewport that fits the box at minimumScale, margins and all, in
// whole pixels: 596 x 416. TableView's minimum size, which the layouts carry up
// to the window.
QSize minimumViewport();

// A played card's height (section 2.7): the larger size up to four seats, the
// smaller from five, where six cards have to share the felt.
qreal playedCardHeight(std::size_t seatCount);

// A CARD'S OR A MARK'S PLACE in table space: its centre, and its turn in DEGREES as
// QGraphicsItem::setRotation() takes it. The played cards, the lead rhombus and
// the cards in your hand are each placed by one (trickitem::cardPlacement(),
// trickitem::leadMarkPlacement(), handitem::cardPlacement()).
struct Placement
{
    QPointF centre;
    qreal rotation = 0.0;
};

// THE STACKING ORDER, bottom to top, in one place: section 3's ladder as the
// redesign's phase 3 set it out. Every item in the scene takes its z from here, so
// no two files can disagree about what is drawn over what.
//
// GAPS OF TEN, so a later phase adds its rung without renumbering the others. The
// bid strip and the modal sheets are widgets over the viewport until phase 10,
// and so above all of these by construction.
//
// The room is not here: it is the view's background, beneath every item.
namespace z
{

// The table and its logo, which are one picture: nothing goes between them.
inline constexpr qreal table = 0.0;

// Under the played cards and under every panel: table.js builds the racks first,
// "so each plaque sits over the base of its own fan".
inline constexpr qreal racks = 10.0;
inline constexpr qreal playedCards = 20.0;
inline constexpr qreal leadMark = 30.0;
inline constexpr qreal hand = 40.0;

// YOUR PLAQUE OVER YOUR HAND. It overlaps the bottom of your fan (section 2.8:
// "the human's panel covers the bottom of the hand; that is intended"), and it is
// the plaque that has to win - it carries your name and your pill.
inline constexpr qreal humanPanel = 50.0;

inline constexpr qreal panels = 60.0;

// The corner panels, over everything on the table: in a window too narrow for
// them they overlap a seat's panel, and they are the ones being read.
inline constexpr qreal hud = 70.0;

} // namespace z

} // namespace tablespace

#endif
