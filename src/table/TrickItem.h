#ifndef TRICK_ITEM_H
#define TRICK_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/Theme.h"

#include <QGraphicsObject>
#include <QPointF>

#include <vector>

class CardItem;
class CardRenderer;

// The trick, growing in the middle of the table.
//
// Each card is placed just inside its owner's seat, so who played what is read
// off the table rather than off a list of names - which is section 14's whole
// argument for a GUI over the terminal's "Ana played the king of hearts".
//
// SEATS COME OFF THE TRICK, NOT OFF ARITHMETIC. PlayedCardView carries
// seatIndex for exactly this reason: getTrickLeaderSeat() has already moved to
// the WINNER by the time the trick-end frame is drawn, so a table rebuilt as
// (leader + i) % playerCount draws the last trick of every round under the wrong
// names. IGameObserver.h says so in its onTrickWon comment, the terminal's
// rebuildTable() carries the same note, and Snapshot.cpp's own rebuildTable()
// has it a third time. This walks snapshot.table and reads the seat off each
// entry.
class TrickItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit TrickItem(const CardRenderer* renderer, QGraphicsItem* parent = nullptr);

    // `positions` is a ring indexed BY SEAT - seatlayout::seatCentres() over
    // seatlayout::ringRect(), so it is the same ordering the plaques use and a
    // card lands on the side of the table its owner sits on.
    //
    // A RING OF ITS OWN, not a fraction of the way to each seat. The obvious
    // version places each card along the line from the middle to its owner, and
    // it is what the first draft did; at six seats on a window wider than it is
    // tall it puts two cards in the same place, because the seat ellipse bunches
    // adjacent directions toward the horizontal. The ring's radius is a question
    // about card size and seat count, which is what ringRect() takes.
    void setTrick(const GameSnapshot& snapshot,
                  const std::vector<QPointF>& positions,
                  QPointF tableCentre,
                  qreal cardHeight,
                  const Theme& theme);

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    // Grown on demand and then reused for the life of the scene, never cleared
    // and rebuilt per frame: six items at most, ~1300 frames a game, and phase
    // E needs a CardItem that outlives the frame it appeared in so that its pos
    // can be animated toward the next one.
    CardItem* cardAt(std::size_t index);

    const CardRenderer* renderer;
    std::vector<CardItem*> cards;

    // Where the lead pip goes, and whether there is one. Section 14 marks the
    // first card played, because "which suit are we following" is the
    // most-asked question at a whist table and is otherwise implicit.
    bool hasLead = false;
    QPointF leadPip;
    qreal pipRadius = 0.0;
    QColor pipColour;
};

#endif
