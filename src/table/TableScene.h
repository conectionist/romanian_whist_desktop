#ifndef TABLE_SCENE_H
#define TABLE_SCENE_H

#include "core/GameSnapshot.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <QGraphicsScene>
#include <QPointF>
#include <QRectF>

#include <memory>
#include <vector>

class SeatItem;
class TrickItem;

// The table: seats on an ellipse, the trick in the middle.
//
// IT READS ONE SNAPSHOT AND NOTHING ELSE. Section 3's cardinal rule is that
// widgets never touch the engine, and the reason is not tidiness: the engine has
// no locking anywhere, so a GUI-thread read racing the game thread is a torn
// read of a hand mid-mutation. Everything drawn here arrives as a value on a
// queued signal.
//
// ITEMS ARE BUILT ONCE AND UPDATED IN PLACE, not cleared and rebuilt per frame.
// A game publishes about 1300 snapshots and rebuilding six plaques and six cards
// for each of them is work for nothing - but the real reason is phase E, which
// animates a CardItem's pos from where it was to where it is going, and cannot
// if the item it was is gone.
class TableScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit TableScene(QObject* parent = nullptr);

    // The whole of the scene's input. Null is ignored rather than treated as an
    // empty game: a queued snapshot is a shared_ptr and nothing should be able
    // to blank the table by dropping one.
    void setSnapshot(std::shared_ptr<const GameSnapshot> snapshot);

    void setTheme(const Theme& theme);

    // Called by TableView when the viewport changes size. Everything below is a
    // fraction of the scene rect, so this is the only thing a resize has to do.
    void relayout();

    // WHAT AN OVERLAY MAY COVER: everything above the lowest seat's plaque.
    // Section 15.1's bid strip is bottom-aligned in it, and phase C3's hand will
    // be.
    //
    // IT HAS TO COME FROM HERE because the lowest plaque is the HUMAN'S - the
    // seated player is drawn at bottom-centre - so "the bottom of the table" and
    // "where the strip goes" are different places. A strip pinned to the
    // viewport's bottom edge draws straight through the player's own name, which
    // is what C2's first run did.
    //
    // AND IT REACHES UP OVER THE TRICK, which is not a compromise. Measured on a
    // 1000x700 table: the trick ring's cards end 565 pixels down and the bottom
    // plaque starts at 578, so the gap between them is thirteen pixels and no
    // strip fits in it - there is no free band to find. What makes covering the
    // middle harmless is WHEN this is used: bidding finishes before the first
    // card of the round is played, so the trick area is empty for the whole time
    // the strip is up. Phase C3's hand is the case to re-ask this for, because a
    // hand IS on screen while cards are down.
    //
    // Empty before the first snapshot; the caller falls back rather than being
    // handed a negative rect.
    QRectF overlayBand() const;

protected:
    // The table surface, through Theme - which is what keeps it replaceable
    // (section 18.1). NOT a QGraphicsRectItem with a brush, which would put a
    // colour decision in the scene.
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    // Grown when a game has more seats than the last one did. Six at most, and
    // reused across games rather than rebuilt, for the reason in the class
    // comment.
    void ensureSeats(std::size_t count);

    // The trick's own ring, and the reason it is not derived from the seats'.
    // See the definitions - the short version is that "a fraction of the way
    // toward the owner" puts two cards in one place at six seats.
    QRectF trickRingRect(std::size_t seatCount, qreal cardHeight) const;
    std::vector<QPointF> trickRing(std::size_t seatCount, qreal cardHeight) const;

    // The rect a plaque's CENTRE may occupy: the scene inset by half a plaque,
    // so the plaque itself lands inside the window.
    QRectF seatRingRect(qreal cardHeight) const;

    // Theme's card height is what the table wants; this is what it can have.
    // They differ only where a plaque and a played card would otherwise
    // overlap - five and six seats, and only in a small window.
    bool fitsOnTable(std::size_t seatCount, qreal cardHeight) const;
    qreal cardHeightFor(std::size_t seatCount) const;

    Theme theme = Theme::light();

    // BY VALUE, so its cache lives as long as the scene and a repaint costs
    // nothing after the first. Declared before the items, which hold a pointer
    // to it: members die in reverse declaration order, so this outlives them.
    CardRenderer cards;

    std::shared_ptr<const GameSnapshot> snapshot;

    std::vector<SeatItem*> seats;
    TrickItem* trick = nullptr;
};

#endif
