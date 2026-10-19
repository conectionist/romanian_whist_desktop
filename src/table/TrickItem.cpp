#include "TrickItem.h"

#include "table/CardItem.h"
#include "table/RackItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"

#include <cmath>

namespace
{

// Whether `seat` is the player's own, drawn at the bottom above their hand.
bool isHuman(unsigned int seatCount, int humanSeat, unsigned int seat)
{
    return humanSeat >= 0 && static_cast<unsigned int>(humanSeat) < seatCount
           && static_cast<unsigned int>(humanSeat) == seat;
}

// The point `radius` from the table's centre at `angle` (radians, y down).
QPointF onCircle(qreal radius, qreal angle)
{
    return tablespace::centre + QPointF(radius * std::cos(angle), radius * std::sin(angle));
}

}

bool trickitem::seated(const GameSnapshot& snapshot)
{
    return snapshot.humanSeat >= 0
           && static_cast<std::size_t>(snapshot.humanSeat) < snapshot.seats.size();
}

std::optional<trickitem::Placement> trickitem::cardPlacement(unsigned int seatCount, int humanSeat,
                                                             unsigned int seat)
{
    const QRectF panel = seatlayout::panelRect(seatCount, humanSeat, seat);

    if(panel.isEmpty())
        return std::nullopt;

    const qreal height = tablespace::playedCardHeight(seatCount);

    if(isHuman(seatCount, humanSeat, seat))
    {
        return Placement{ QPointF(tablespace::centre.x(),
                                  tablespace::handTop - tablespace::playedInset - height / 2.0),
                          0.0 };
    }

    // The felt's edge, the inset, then half the card: table.js trickPos().
    const qreal angle = seatlayout::rackAngle(panel);
    const qreal radius = tablespace::feltRadius - tablespace::playedInset - height / 2.0;

    return Placement{ onCircle(radius, angle), rackitem::frameRotation(angle) };
}

std::optional<trickitem::Placement> trickitem::leadMarkPlacement(unsigned int seatCount, int humanSeat,
                                                                 unsigned int seat)
{
    const QRectF panel = seatlayout::panelRect(seatCount, humanSeat, seat);

    if(panel.isEmpty())
        return std::nullopt;

    if(isHuman(seatCount, humanSeat, seat))
        return Placement{ QPointF(tablespace::centre.x(), tablespace::leadMarkHumanY), 0.0 };

    const qreal angle = seatlayout::rackAngle(panel);

    return Placement{ onCircle(tablespace::goldLineRadius, angle), rackitem::frameRotation(angle) };
}

std::optional<unsigned int> trickitem::leadSeat(const GameSnapshot& snapshot)
{
    // In the play phase only, as table.js draws it: from the completed bids,
    // before the first card is asked for, until the round's last trick is won.
    if(snapshot.seats.empty() || snapshot.phase != romanian_whist::GamePhase::Playing)
        return std::nullopt;

    // The first card's owner - and still while a full trick waits to be cleared,
    // which is when the engine's own leader has already moved on to the winner.
    if(!snapshot.table.empty())
        return snapshot.table.front().seatIndex;

    // Nothing down yet: the engine's leader for the trick about to start.
    return snapshot.leadSeat;
}

TrickItem::TrickItem(const CardRenderer* renderer, cardshadow::Cache* shadows, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , renderer(renderer)
    , shadows(shadows ? shadows : &ownShadows)
{
    setFlag(QGraphicsItem::ItemHasNoContents);
}

CardItem* TrickItem::cardAt(std::size_t index)
{
    while(cards.size() <= index)
        cards.push_back(new CardItem(renderer, this));

    return cards[index];
}

void TrickItem::setTrick(const GameSnapshot& snapshot, const Theme& theme)
{
    const unsigned int seatCount = static_cast<unsigned int>(snapshot.seats.size());
    const qreal cardHeight = tablespace::playedCardHeight(seatCount);

    // A WINNER ONCE THERE IS SOMETHING TO BEAT. The engine ranks a lone first card
    // as winning, which it is; table.js highlights from the second card on, since
    // until then the highlight says nothing the card does not.
    const bool contested = snapshot.table.size() >= 2;

    for(std::size_t i = 0 ; i < snapshot.table.size() ; i++)
    {
        const PlayedCardView& played = snapshot.table[i];

        // Read off the entry, never derived from leadSeat and an offset - see
        // the header. A seat this table cannot place is a snapshot this scene
        // cannot draw, and leaving it off is better than inventing a place.
        const std::optional<trickitem::Placement> place =
            trickitem::cardPlacement(seatCount, snapshot.humanSeat, played.seatIndex);

        if(!place)
        {
            // HIDDEN, not merely skipped. The item at this index may already be
            // on the table from a previous frame, and the hide pass below starts
            // past the end of THIS table - so an index inside it is one nothing
            // else reaches, and a bare `continue` leaves the last frame's card
            // sitting there with its face, its turn and its winning highlight.
            cardAt(i)->hide();
            continue;
        }

        CardItem* item = cardAt(i);

        item->setCardHeight(cardHeight);
        item->setFace(played.card);
        item->setPos(place->centre);
        item->setRotation(place->rotation);

        // THE HIGHLIGHT IS isWinning AND IS NEVER RECOMPUTED. It is the engine's
        // own ranking of a partly played trick, filled from
        // getCurrentTrickLeader(), so it cannot disagree with the winner the
        // engine eventually declares.
        item->setPlayed(contested && played.isWinning, theme.table, shadows);

        item->setZValue(static_cast<qreal>(i));
        item->show();
    }

    // Hidden rather than deleted, for the reason cardAt() gives.
    for(std::size_t i = snapshot.table.size() ; i < cards.size() ; i++)
        cards[i]->hide();
}

QRectF TrickItem::boundingRect() const
{
    // The cards are children and carry their own rects; this paints nothing.
    return QRectF();
}

void TrickItem::paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*)
{
}
