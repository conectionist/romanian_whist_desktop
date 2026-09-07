#ifndef SEAT_ITEM_H
#define SEAT_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/Theme.h"

#include <QGraphicsObject>

// One seat's plaque: name, the bid/won pill, the cards left as a stack, and the
// turn ring (section 14).
//
// A QGraphicsObject for the same reason CardItem is one - section 18.2 fades the
// turn ring in over 120ms in phase E, which is a property animation on an object.
//
// THE TURN RING IS DRIVEN BY snapshot.activeSeat AND NOTHING ELSE. Section 14 is
// explicit that it must never be counted forward from the leader, and the
// snapshot's own comment says why activeSeat is the right field: the engine
// engages it from just before a provider is asked until just after its answer is
// recorded, so it stays on the seat that just bid or just played and goes empty
// between turns without any callback having to remember to say so.
class SeatItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit SeatItem(QGraphicsItem* parent = nullptr);

    // What a plaque occupies at a given card height, ring included.
    //
    // Static and public because the LAYOUT needs it before any seat exists:
    // seatlayout::seatCentres() places centres inside the rect it is given, and
    // a centre inside the rect is not the same as a plaque inside it - the two
    // side seats hang half a plaque past the edge otherwise. TableScene insets
    // by this before laying out, which is the same "the caller passes a shorter
    // rect" the hand strip will use in B2b.
    static QSizeF plaqueSize(qreal cardHeight);

    // Everything this draws, from one snapshot's SeatView plus the two facts
    // that are not on it. Taken as a whole rather than as a dozen setters
    // because it is repainted from a whole snapshot ~1300 times a game and a
    // half-updated plaque is a state nothing should be able to reach.
    void setSeat(const SeatView& seat, bool active, const Theme& theme);

    // In LOGICAL pixels. The plaque derives from the card height, which section
    // 18.1 makes the one unit every other metric on the table comes from.
    void setCardHeight(qreal height);

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    // Section 14's three states. `NoBid` is the fourth thing that can be true
    // and is not a state of the pill at all: hasBet tells a genuine bid of zero
    // from no bid yet, and a seat that has not bid must not read as on target.
    enum class Pill
    {
        NoBid,
        OnTarget,
        Owing,
        Over
    };

    Pill pillState() const;
    QColor pillColour() const;

    SeatView seat;
    bool active = false;
    Theme theme = Theme::light();
    qreal cardHeight = 0.0;
};

#endif
