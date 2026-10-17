#ifndef SEAT_ITEM_H
#define SEAT_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/Theme.h"

#include <QGraphicsObject>
#include <QRectF>
#include <QSizeF>

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
class CardItem;
class CardRenderer;

// WHERE THE THINGS ON A PLAQUE GO, which is the part of this item with a right
// answer - so it is a free function, the split SnapshotLog.h argues for and
// seatlayout, infostrip and bidstrip all follow. It needs no scene, no painter
// and no seat to have that answer checked.
namespace seatitem
{

struct Layout
{
    // The rounded rect itself, centred on the item's origin.
    QRectF plaque;

    // The column the name and the pill get. NARROWER when a card is shown, and
    // that is the whole reason this is computed in one place: the name is elided
    // against this width, and a name measured against the full plaque while
    // drawn into a narrowed one runs under the card. B2b's review found four
    // defects of exactly that shape.
    QRectF text;

    // The face-up card, EMPTY when this seat is not showing one - which is every
    // round but a Forehead one, and every seat but another player's.
    QRectF card;

    // Where the cards-left stack is drawn instead. Its right edge is the
    // plaque's, so the stack grows toward the middle and never reaches the name.
    QRectF stack;
};

// `faceSize` is the card's drawn size, or an empty size when there is no card to
// show. Taken as a parameter rather than derived here because the aspect belongs
// to CardRenderer, which section 18.1 makes the only thing that knows what a card
// looks like.
Layout layoutFor(qreal cardHeight, QSizeF faceSize);

// The height a face-up card is drawn at. A fraction of the PLAQUE and not of the
// card unit, because what it has to fit inside is the plaque - and the plaque's
// padded inner box is only 0.38 card-heights, which is too short for anything
// legible. This uses the plaque's full height less a margin instead.
qreal faceHeightFor(qreal cardHeight);

} // namespace seatitem

class SeatItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // The renderer outlives every item, as CardItem documents: TableScene owns it
    // by value and the items are its children.
    //
    // TAKEN SINCE C4, which is when a plaque first had a card to draw. Section 17
    // puts another seat's Forehead card face-up on their plaque, and that card
    // comes through the same seam every other card on the table does.
    explicit SeatItem(const CardRenderer* renderer, QGraphicsItem* parent = nullptr);

    // What a plaque occupies at a given card height, ring included.
    //
    // Static and public because the LAYOUT needs it before any seat exists:
    // TableScene stops the bid strip's band above the lowest plaque with it.
    // Until the table redesign's phase 2 it also inset the rect the seat ring
    // was laid in, so that a plaque, and not just its centre, fitted the window.
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

    // WHAT THIS PLAQUE DECIDED, exposed because it is a pure query and because
    // the decision has two consequences a test cannot otherwise separate: the
    // card is one, and the narrowed text column and dropped stack are the other.
    // A seat wrongly believing it has a card to show still hides the card - the
    // hand is checked again where it is dereferenced - and would silently lay
    // itself out as though one were there.
    seatitem::Layout layoutFor() const;

    bool showsFace() const;

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

    // Places the face-up card, or hides it.
    void refreshFace();

    const CardRenderer* renderer;

    // ONE CARD, and a child rather than something this paints. CardItem already
    // resolves the device pixel ratio off the widget being painted into, so a
    // window dragged to another screen re-rasterizes rather than scaling what it
    // had - reusing it inherits that instead of solving it a second time here.
    //
    // Hidden rather than deleted between rounds, because a plaque shows a card
    // for two rounds of twenty-six and rebuilding one per snapshot is the churn
    // TableScene's class comment rules out.
    CardItem* face = nullptr;

    SeatView seat;
    bool active = false;
    Theme theme = Theme::light();
    qreal cardHeight = 0.0;
};

#endif
