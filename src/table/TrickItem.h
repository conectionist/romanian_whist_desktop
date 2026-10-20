#ifndef TRICK_ITEM_H
#define TRICK_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/CardShadow.h"
#include "paint/Theme.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"

#include <QGraphicsObject>
#include <QPointF>

#include <optional>
#include <vector>

class CardItem;
class CardRenderer;

// WHERE THE TRICK GOES, which is the part of it with a right answer - so free
// functions, the split rackitem and seatlayout follow, checked with no scene.
//
// The table redesign's section 2.7, from table.js trickPos() and renderTrick():
// each opponent's card lands squarely in front of their rack, turned to line up
// with it, and yours just above your hand.
namespace trickitem
{

// WHERE `seat`'S PLAYED CARD LIES.
//
// An opponent's - and a spectated seat 0's, which is drawn as one - is on the line
// from the table's centre at its rack's angle (seatlayout::rackAngle()), its outer
// end tablespace::playedInset inside the felt's edge, and turned as the rack is
// (rackitem::frameRotation()): so the side seats' cards lie sideways and the top
// seat's is upside down, each reading the right way up to its owner. A quarter turn
// exactly when it is one.
//
// Yours is centred above your hand, its bottom edge playedInset above the hand's
// top, and not turned.
//
// From the seat's place, worked out once (seatlayout::place()); the card's size
// is the table's, by `seatCount`.
tablespace::Placement cardPlacement(unsigned int seatCount, const seatlayout::Place& place);

// The same for a seat by number: none for a seat with no panel - a player count
// section 2.5 has no row for, or a seat that is not at the table.
std::optional<tablespace::Placement> cardPlacement(unsigned int seatCount, int humanSeat, unsigned int seat);

// WHERE THE LEAD RHOMBUS GOES FOR `seat`: an opponent's on the felt's gold circle
// at its rack's angle, between the rack and the card, turned so its long axis
// points at the rack; yours in the gap between your card and your hand, where the
// circle is under your cards. None where cardPlacement() has none.
tablespace::Placement leadMarkPlacement(const seatlayout::Place& place);

std::optional<tablespace::Placement> leadMarkPlacement(unsigned int seatCount, int humanSeat,
                                                       unsigned int seat);

// WHICH SEAT THE RHOMBUS MARKS, if any: only while the engine's phase is Playing,
// and then whoever played the trick's first card - or, before it is down,
// snapshot.leadSeat, the engine's leader for the trick about to start.
//
// THE PHASE, NOT THE BIDS AND THE SEAT ON TURN. After the last bid is recorded the
// bidder is still on turn, and after the round's last trick the finished trick is
// still on the table; neither is a trick being led, and only the phase says so.
std::optional<unsigned int> leadSeat(const GameSnapshot& snapshot);

} // namespace trickitem

// The trick, growing in the middle of the table.
//
// Each card lies in front of its owner's rack, so who played what is read off the
// table rather than off a list of names - which is section 14's whole argument for
// a GUI over the terminal's "Ana played the king of hearts".
//
// SEATS COME OFF THE TRICK, NOT OFF ARITHMETIC. PlayedCardView carries
// seatIndex for exactly this reason: getTrickLeaderSeat() has already moved to
// the WINNER by the time the trick-end frame is drawn, so a table rebuilt as
// (leader + i) % playerCount draws the last trick of every round under the wrong
// names. IGameObserver.h says so in its onTrickWon comment, the terminal's
// rebuildTable() carries the same note, and Snapshot.cpp's own rebuildTable()
// has it a third time. This walks snapshot.table and reads the seat off each
// entry.
//
// IT DRAWS NOTHING ITSELF: the cards are its children, and the lead rhombus is
// LeadMarkItem, a rung above them in tablespace::z.
class TrickItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // The renderer outlives every item, as CardItem documents. `shadows` is the
    // played cards' shadow cache, owned with it; null gives the item one of its own.
    explicit TrickItem(const CardRenderer* renderer, cardshadow::Cache* shadows = nullptr,
                       QGraphicsItem* parent = nullptr);

    // Every card on the table, each where trickitem::cardPlacement() puts it.
    // `places` is every seat's, seatlayout::places() for the snapshot's table, as
    // TableScene has already worked them out for the racks.
    void setTrick(const GameSnapshot& snapshot, const seatlayout::Places& places, const Theme& theme);

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    // Grown on demand and then reused for the life of the scene, never cleared
    // and rebuilt per frame: six items at most, ~1300 frames a game, and phase
    // E needs a CardItem that outlives the frame it appeared in so that its pos
    // can be animated toward the next one.
    CardItem* cardAt(std::size_t index);

    const CardRenderer* renderer;

    cardshadow::Cache ownShadows;
    cardshadow::Cache* shadows;

    std::vector<CardItem*> cards;
};

#endif
