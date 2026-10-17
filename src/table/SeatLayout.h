#ifndef SEAT_LAYOUT_H
#define SEAT_LAYOUT_H

#include <QPointF>
#include <QRectF>

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
// seats must still come from here, in fractions of the rect they are laid in -
// otherwise the coordinates silently become properties of one image's
// perspective and the background stops being replaceable. Section 18.1 is the
// general form of that rule and Theme is its other half.
namespace seatlayout
{

// Seat centres, in seat order, inside `table`.
//
// `humanSeat` is the snapshot's: an index, or -1 for a spectator game. The human
// is placed at bottom-centre and the rest run CLOCKWISE from there in seat
// order, so the play order on screen matches the play order in the game.
//
// A SPECTATOR GAME IS NOT A SEPARATE LAYOUT. Section 14 says a spectated table
// distributes every seat around the full ellipse, which is what this already
// does with seat 0 at the bottom - so -1 is handled by placing seat 0 where the
// human would have gone, and nothing else changes. The real difference is that
// no bottom strip is reserved for a hand, and that is the caller's business:
// it passes a shorter rect.
//
// Returns an empty vector for `seatCount == 0` or an empty rect. `humanSeat`
// outside the table is read as -1 rather than trusted into a modulo.
std::vector<QPointF> seatCentres(unsigned int seatCount, int humanSeat, QRectF table);

// The rect to hand seatCentres() when what you know is the radius you want
// rather than the space you have. The ring it produces is a CIRCLE of that
// radius about `centre`.
//
// The trick is laid out this way and the seats are not, which is the whole
// reason this exists. Seats want the room the window has, so they take an
// ellipse of it. The trick wants its cards not to touch each other, which is a
// question about the card size and the number of seats and not about the window
// at all - and an ellipse is exactly wrong for it, because a wide one bunches
// adjacent directions toward the horizontal and puts two cards in one place.
QRectF ringRect(QPointF centre, qreal radius);

// The same with two radii, so the ring is an ELLIPSE of exactly those radii about
// `centre`. The one-radius form above is this with both equal.
//
// For a layout that knows the shape it wants in a fixed space rather than the
// room a window happens to have - the table redesign's phase 2 lays the seats on
// one such ellipse round the table until phase 4 gives every seat its own spot.
QRectF ringRect(QPointF centre, qreal radiusX, qreal radiusY);

} // namespace seatlayout

#endif
