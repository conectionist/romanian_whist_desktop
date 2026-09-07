#include "TrickItem.h"

#include "table/CardItem.h"

#include <QPainter>
#include <QtMath>

#include <cmath>

namespace
{

// Rotated slightly toward the centre, so the trick looks laid down rather than
// pasted on. Degrees at the extreme; a seat directly above or below is not
// rotated at all.
//
// KEPT SMALL BECAUSE IT COSTS CLEARANCE. A card tilted by t occupies
// w*cos(t) + h*sin(t) across, which at nine degrees is a sixth wider than the
// card - and that sixth comes straight off the gap the ring was sized to leave.
constexpr qreal maximumTilt = 6.0;

constexpr qreal pipRadiusInCards = 0.045;
constexpr qreal pipDropInCards = 0.60;

}

TrickItem::TrickItem(const CardRenderer* renderer, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , renderer(renderer)
{
}

QSizeF TrickItem::cardFootprint(QSizeF cardSize)
{
    const qreal tilt = qDegreesToRadians(maximumTilt);
    const qreal cosine = std::cos(tilt);
    const qreal sine = std::sin(tilt);

    return QSizeF(cardSize.width() * cosine + cardSize.height() * sine,
                  cardSize.width() * sine + cardSize.height() * cosine);
}

CardItem* TrickItem::cardAt(std::size_t index)
{
    while(cards.size() <= index)
        cards.push_back(new CardItem(renderer, this));

    return cards[index];
}

void TrickItem::setTrick(const GameSnapshot& snapshot,
                         const std::vector<QPointF>& positions,
                         QPointF tableCentre,
                         qreal cardHeight,
                         const Theme& theme)
{
    prepareGeometryChange();

    for(std::size_t i = 0 ; i < snapshot.table.size() ; i++)
    {
        const PlayedCardView& played = snapshot.table[i];

        // Read off the entry, never derived from leadSeat and an offset - see
        // the header. A seat index the layout does not have is a snapshot this
        // scene cannot draw, and skipping it is better than indexing past the
        // end of the vector.
        if(played.seatIndex >= positions.size())
        {
            // HIDDEN, not merely skipped. The item at this index may already be
            // on the table from a previous frame, and the hide pass below starts
            // past the end of THIS table - so an index inside it is one nothing
            // else reaches, and a bare `continue` leaves the last frame's card
            // sitting there with its face, its tilt and its winning highlight.
            cardAt(i)->hide();
            continue;
        }

        CardItem* item = cardAt(i);

        const QPointF seat = positions[played.seatIndex];
        const QPointF toward = seat - tableCentre;

        item->setCardHeight(cardHeight);
        item->setFace(played.card);
        item->setPos(seat);

        // THE HIGHLIGHT IS isWinning AND IS NEVER RECOMPUTED. It is the engine's
        // own ranking of a partly played trick, filled from
        // getCurrentTrickLeader(), so it cannot disagree with the winner the
        // engine eventually declares.
        item->setWinning(played.isWinning, theme.accent);

        // Tilted by how far round the table the player sits, so seats opposite
        // each other lean opposite ways. Zero for a seat straight above or
        // below, which is where an unrotated card looks right anyway.
        const qreal spread = qFuzzyIsNull(toward.y()) && qFuzzyIsNull(toward.x())
                                 ? 0.0
                                 : toward.x() / std::max(1.0, std::hypot(toward.x(), toward.y()));

        item->setRotation(-spread * maximumTilt);
        item->setZValue(static_cast<qreal>(i));
        item->show();
    }

    // Hidden rather than deleted, for the reason cardAt() gives.
    for(std::size_t i = snapshot.table.size() ; i < cards.size() ; i++)
        cards[i]->hide();

    hasLead = !snapshot.table.empty() && snapshot.table.front().seatIndex < positions.size();

    if(hasLead)
    {
        const QPointF card = positions[snapshot.table.front().seatIndex];

        leadPip = card + QPointF(0.0, cardHeight * pipDropInCards);
        pipRadius = cardHeight * pipRadiusInCards;
        pipColour = theme.accent;
    }

    update();
}

QRectF TrickItem::boundingRect() const
{
    // The cards are children and carry their own rects; this only has to cover
    // what THIS paints, which is the pip.
    if(!hasLead)
        return QRectF();

    return QRectF(leadPip.x() - pipRadius, leadPip.y() - pipRadius,
                  pipRadius * 2.0, pipRadius * 2.0);
}

void TrickItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(!hasLead || pipRadius <= 0.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(pipColour);
    painter->drawEllipse(leadPip, pipRadius, pipRadius);
}
