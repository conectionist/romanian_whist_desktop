#ifndef BID_BUBBLE_ITEM_H
#define BID_BUBBLE_ITEM_H

#include "paint/PictureMemo.h"
#include "paint/Theme.h"
#include "table/Motion.h"

#include <QGraphicsObject>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <cstddef>

class QVariantAnimation;

// A BID, SAID OUT LOUD (the table redesign's phase 12): a small gold pill that
// floats over a seat's panel for a moment as the bid is made - "Bids 2" for a bot,
// "You bid 2" for you - as page.html's .bubble does.
//
// The bid is on the scoreboard and on the seat's pill as well; this is only the
// moment it is made, which the table otherwise shows as a number changing.
namespace bidbubble
{

// The words: yours, or anyone else's.
QString textFor(bool yours, unsigned int bet);

// The pill's size for `text`: Marcellus 19 inside 4 above and below, 14 either side.
QSizeF sizeFor(const QString& text);

// WHERE IT GOES over a panel whose rect is `panel`, in table space: centred on it,
// its top 18 above the panel's padding box (`top: -18px` inside a 1-unit border).
QRectF rectOver(const QRectF& panel, const QString& text);

// HOW IT LOOKS `progress` of the way through its 1.6 s (page.html's @keyframes
// bubble): rising 6 and fading in over the first 12%, held to 80%, then fading out.
// Each stretch eases out, as CSS applies `ease-out` to each.
struct Frame
{
    qreal opacity = 1.0;
    qreal rise = 0.0;    // how far below its place it still is, in table units
};

Frame frameAt(qreal progress);

} // namespace bidbubble

class BidBubbleItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit BidBubbleItem(QGraphicsItem* parent = nullptr);

    // Shows `text` over the panel at `panel` and lets it go by itself: 1.6 s, or at
    // Off held still for 1.5 s and then gone (the mock-up's reduced-motion rule). A
    // new bid replaces the one showing, from the start.
    void say(const QString& text, const QRectF& panel, motion::Pace pace, const Theme::TablePalette& palette);

    // Gone now: a new game, or a table with fewer seats.
    void dismiss();

    // What it says, and the animation that will take it away: READ-ONLY and for tests,
    // which drive the clock by hand.
    QString text() const { return words; }
    QVariantAnimation* clock() const { return timeline; }

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    void step(qreal progress);

    QString words;
    QSizeF pill;
    bool still = false;

    Theme::TablePalette palette;

    QVariantAnimation* timeline = nullptr;

    // One cached picture: the pill, its words and its shadow's blur.
    mutable PictureMemo memo;
};

#endif
