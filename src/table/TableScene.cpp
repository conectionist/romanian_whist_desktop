#include "TableScene.h"

#include "table/SeatItem.h"
#include "table/SeatLayout.h"
#include "table/TrickItem.h"

#include <QPainter>

#include <cmath>

namespace
{

// The gap between two neighbouring cards in the trick, as a fraction of what a
// card occupies. Small, because the trick should read as one group - but not
// zero, because two cards that merely touch look like a mistake.
constexpr qreal trickGap = 0.14;

}

// A CIRCLE SIZED SO THE CARDS DO NOT TOUCH, which is a question about the card
// and the seat count and not about the window.
//
// n cards evenly spaced on a circle of radius r sit 2*r*sin(pi/n) apart, so
// requiring that to clear what a card occupies inverts to the radius below. Six
// seats need a wider ring than four for the same cards, which is the case the
// first draft got wrong: it placed each card a fixed fraction of the way toward
// its owner, and at six seats on a window wider than it is tall two cards landed
// on top of each other, because the seat ellipse bunches adjacent directions
// toward the horizontal.
//
// MEASURED OFF THE CARD RATHER THAN TUNED. The second draft used a constant
// multiple of the card HEIGHT and still overlapped, because a card tilted by six
// degrees is 1.067 card heights tall and the constant was 1.10 - a three-pixel
// gap that reads as touching. Taking the footprint from the item that does the
// rotating leaves nothing to keep in step by hand, and the tilt can change
// without this being re-derived.
//
// Card-against-card is settled HERE and not in fitsOnTable(), because it has to
// be: the ring and the cards scale together, so a ring whose neighbours overlap
// overlaps at every card size and shrinking cannot fix it.
QRectF TableScene::trickRingRect(std::size_t seatCount, qreal cardHeight) const
{
    if(seatCount == 0 || cardHeight <= 0.0)
        return QRectF();

    const QSizeF footprint = TrickItem::cardFootprint(cards.sizeForHeight(cardHeight));

    // The larger side, because two neighbours may be stacked or side by side
    // depending on where round the ring they sit.
    const qreal occupies = std::max(footprint.width(), footprint.height());

    const qreal spacing = occupies * (1.0 + trickGap);
    const qreal radius = spacing / (2.0 * std::sin(M_PI / static_cast<qreal>(seatCount)));

    return seatlayout::ringRect(sceneRect().center(), radius);
}

std::vector<QPointF> TableScene::trickRing(std::size_t seatCount, qreal cardHeight) const
{
    // The SAME function the plaques use, over a different rect - so the two
    // rings cannot drift out of step, and a card is always on the side of the
    // table its owner sits on.
    return seatlayout::seatCentres(static_cast<unsigned int>(seatCount),
                                   snapshot ? snapshot->humanSeat : -1,
                                   trickRingRect(seatCount, cardHeight));
}

QRectF TableScene::seatRingRect(qreal cardHeight) const
{
    // INSET BY HALF A PLAQUE. seatlayout puts CENTRES inside the rect it is
    // given, which is not the same as putting plaques inside it: at the two side
    // seats the centre sits a radius from the middle and the plaque hangs half
    // its width further, off the edge of the window.
    const QSizeF plaque = SeatItem::plaqueSize(cardHeight);

    return sceneRect().adjusted(plaque.width() / 2.0, plaque.height() / 2.0,
                                -plaque.width() / 2.0, -plaque.height() / 2.0);
}

// Whether a table of this many seats can be drawn at this card size without a
// played card ending up underneath a plaque.
//
// ASKED OF THE GEOMETRY RATHER THAN OF A TUNED CONSTANT. Both rings are built
// and the rectangles are compared, which is the same question in every
// direction at once and for every seat count - where the arithmetic version
// would be a separate inequality for the vertical and the horizontal, and would
// be re-derived by hand every time a plaque or a tilt changed.
bool TableScene::fitsOnTable(std::size_t seatCount, qreal cardHeight) const
{
    const QRectF within = seatRingRect(cardHeight);

    if(within.isEmpty())
        return false;

    const std::vector<QPointF> plaques =
        seatlayout::seatCentres(static_cast<unsigned int>(seatCount),
                                snapshot ? snapshot->humanSeat : -1, within);

    const std::vector<QPointF> ring = trickRing(seatCount, cardHeight);

    if(plaques.size() != seatCount || ring.size() != seatCount)
        return false;

    const QSizeF plaque = SeatItem::plaqueSize(cardHeight);
    const QSizeF card = TrickItem::cardFootprint(cards.sizeForHeight(cardHeight));

    for(const QPointF& position : ring)
    {
        const QRectF cardRect(position.x() - card.width() / 2.0,
                              position.y() - card.height() / 2.0,
                              card.width(), card.height());

        for(const QPointF& seat : plaques)
        {
            const QRectF plaqueRect(seat.x() - plaque.width() / 2.0,
                                    seat.y() - plaque.height() / 2.0,
                                    plaque.width(), plaque.height());

            if(cardRect.intersects(plaqueRect))
                return false;
        }
    }

    return true;
}

// Theme's card height is the size the table WANTS; this is the size it can have.
//
// Section 18.1's unit is derived in Theme from the hand fan - eight cards across
// the bottom - and that is the right constraint for four seats and the wrong one
// for six, where the same card has to fit between two plaques on each side as
// well. Rather than re-tune the unit for the worst case and leave every
// four-seat table smaller than it needs to be, the scene keeps Theme's answer
// when it fits and shrinks toward one that does when it does not.
//
// Bisection rather than a formula because fitsOnTable() is a rectangle test and
// not an inequality that can be inverted. Twenty-two halvings of a range under a
// thousand pixels settles well inside a pixel, and it runs only when the
// preferred height does not fit - which for a four-seat game is never.
qreal TableScene::cardHeightFor(std::size_t seatCount) const
{
    const qreal preferred = theme.cardHeightFor(sceneRect().size().toSize());

    if(seatCount == 0 || fitsOnTable(seatCount, preferred))
        return preferred;

    qreal tooSmall = 1.0;
    qreal tooBig = preferred;

    for(int step = 0 ; step < 22 ; step++)
    {
        const qreal middle = (tooSmall + tooBig) / 2.0;

        if(fitsOnTable(seatCount, middle))
            tooSmall = middle;
        else
            tooBig = middle;
    }

    return tooSmall;
}

TableScene::TableScene(QObject* parent)
    : QGraphicsScene(parent)
{
    trick = new TrickItem(&cards);
    addItem(trick);
}

void TableScene::setSnapshot(std::shared_ptr<const GameSnapshot> next)
{
    if(!next)
        return;

    snapshot = std::move(next);
    relayout();
}

void TableScene::setTheme(const Theme& palette)
{
    theme = palette;

    // The background is drawn by drawBackground() rather than held in an item,
    // so this has to say so - nothing else would repaint it.
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
    relayout();
}

void TableScene::ensureSeats(std::size_t count)
{
    while(seats.size() < count)
    {
        SeatItem* seat = new SeatItem();
        addItem(seat);
        seats.push_back(seat);
    }

    // Hidden rather than deleted: a second game may seat more players again,
    // and phase D starts one in the same process.
    for(std::size_t i = count ; i < seats.size() ; i++)
        seats[i]->hide();
}

void TableScene::relayout()
{
    if(!snapshot || sceneRect().isEmpty())
        return;

    ensureSeats(snapshot->seats.size());

    // THE ONE UNIT - Theme's, reduced only where this table cannot hold it. See
    // cardHeightFor(). Nothing else in this file picks a card size (18.1).
    const qreal cardHeight = cardHeightFor(snapshot->seats.size());

    const std::vector<QPointF> centres =
        seatlayout::seatCentres(static_cast<unsigned int>(snapshot->seats.size()),
                                snapshot->humanSeat, seatRingRect(cardHeight));

    if(centres.size() != snapshot->seats.size())
        return;

    for(std::size_t i = 0 ; i < snapshot->seats.size() ; i++)
    {
        SeatItem* item = seats[i];

        item->setCardHeight(cardHeight);
        item->setPos(centres[i]);

        // activeSeat is -1 between turns, which is a real state and not a
        // missing one: no ring anywhere is what the table should show while
        // nobody is being asked to move.
        const bool active = snapshot->activeSeat >= 0
                            && static_cast<std::size_t>(snapshot->activeSeat) == i;

        item->setSeat(snapshot->seats[i], active, theme);
        item->show();
    }

    trick->setTrick(*snapshot, trickRing(snapshot->seats.size(), cardHeight),
                    sceneRect().center(), cardHeight, theme);
}

void TableScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    const QRectF scene = sceneRect();

    if(scene.isEmpty())
        return;

    // Off the paint device, so a window on a HiDPI screen gets a background
    // rasterized for it rather than one scaled up - the half of B1's dpr
    // contract that applies to the table rather than to the cards.
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;

    const QPixmap background = theme.background(scene.size().toSize(), dpr);

    if(background.isNull())
        return;

    // Only the exposed part, which is what `rect` is for: a card moving in the
    // middle of the table must not repaint the whole surface behind it.
    painter->drawPixmap(rect, background, rect.translated(-scene.topLeft()));
}
