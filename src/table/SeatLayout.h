#ifndef SEAT_LAYOUT_H
#define SEAT_LAYOUT_H

#include <QPointF>
#include <QRectF>

#include <optional>
#include <vector>

// Where the seats go, and the only arithmetic in phase B.
//
// A free function rather than a TableScene member, on the same grounds
// SnapshotLog gives for being one: it is the part of the table with a right
// answer, so it is the part worth a test - and it needs no scene, no widget, no
// engine and no thread to have that answer checked.
//
// SEAT POSITIONS ARE COMPUTED, NEVER READ OFF THE ARTWORK (section 14). If the
// table surface ever becomes an illustration with players drawn into it, the
// seats must still come from here -
// otherwise the coordinates silently become properties of one image's
// perspective and the background stops being replaceable. Section 18.1 is the
// general form of that rule and Theme is its other half.
namespace seatlayout
{

// WHETHER SOMEBODY IS SITTING AT THE TABLE: `humanSeat` is the snapshot's, an index
// or -1 for a spectator game, and an index past the table is read as -1 rather
// than trusted. The one place that rule lives - the bottom seat, the hand, and
// whether the bottom card goes above it all follow it.
bool humanSeated(unsigned int seatCount, int humanSeat);

// How many seats clockwise `seat` is from the one at the bottom - yours, or seat
// 0's in a spectator game. 0 for the bottom seat itself. panelRect() places a seat
// by this, and so everything placed from a panel - its rack, its played card -
// follows the same order.
//
// `humanSeat` is the snapshot's: an index, or -1 for a spectator game. The human
// is at the bottom and the rest run CLOCKWISE from there in seat order, so the
// play order on screen matches the play order in the game.
//
// A SPECTATOR GAME IS NOT A SEPARATE LAYOUT (section 14): seat 0 goes where the
// human would have gone, and nothing else changes. The real difference is that
// there is no hand, and that is the caller's business.
//
// `humanSeat` outside the table is read as -1 rather than trusted into a modulo.
unsigned int stepsFromBottom(unsigned int seatCount, int humanSeat, unsigned int seat);

// A SEAT'S NAME PANEL, in table space: the redesign's section 2.5, which places
// each panel by hand per player count so that it sits just outside the wooden rim
// and clears the corner panels. The bottom seat gets tablespace::bottomPanel and
// every other one a tablespace::panelSize at its spot.
//
// NOT ON A RING: no ellipse puts a 280-wide panel just
// outside a circle at every angle, which is why the mock-up gave up on one. The
// order is clockwise from the bottom, through stepsFromBottom().
//
// Empty for a player count the table has no row for (outside 2 to 6) or a seat
// that is not at the table.
QRectF panelRect(unsigned int seatCount, int humanSeat, unsigned int seat);

// WHERE A SEAT'S RACK STANDS ON THE RIM (section 2.6, table.js buildSeats()): the
// angle from the table's centre, in RADIANS with y down, so 0 is to the right and
// pi/2 straight down - the bottom seat's.
//
// In line with the panel's centre, unless the rim there is under the panel itself:
// then the rack slides CLOCKWISE along the rim, a degree at a time, until it is
// clear of the panel grown by 10, and a further 9 degrees so the width of its row
// clears the panel's corner too. With section 2.5's panels it never has to - every
// rim point is already clear - but a panel moved closer would need it, and the
// mock-up states the rule rather than the answer.
//
// The angle is what the played card and the lead rhombus line up with from phase 6.
//
// FOR A SEAT, READ place(), which has no answer for a seat with no panel. A seat
// form of this used to return 0 there, which is also the right-hand seat's real
// angle (PHASE5_REVIEW_FINDINGS.md #5).
qreal rackAngle(QRectF panel);

// The point `radius` from the table's centre at `angle` (radians, y down): a rack's
// origin, a played card's centre, the lead rhombus's.
QPointF onCircle(qreal angle, qreal radius);

// The point on the rim a rack stands at: tablespace::rackRadius from the centre,
// at `angle`.
QPointF rackOrigin(qreal angle);

// WHERE A SEAT IS, worked out once: its panel, whether it is the human's - drawn at
// the bottom above their hand rather than on the rim - and otherwise its rack's
// angle, which its rack, its played card and the lead rhombus all line up with.
struct Place
{
    QRectF panel;
    bool human = false;
    qreal angle = 0.0;      // rackAngle(panel); 0 for the human's seat, which has no rack
};

// None for a seat with no panel, as panelRect() has none.
std::optional<Place> place(unsigned int seatCount, int humanSeat, unsigned int seat);

// Every seat's, in seat order: what TableScene works out once per relayout and
// hands to the racks, the trick and the rhombus, so rackAngle()'s slide runs once a
// seat rather than once for each of them.
using Places = std::vector<std::optional<Place>>;

Places places(unsigned int seatCount, int humanSeat);

} // namespace seatlayout

#endif
