#include "HandItem.h"

#include "paint/CardRenderer.h"
#include "table/CardItem.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>

#include <algorithm>

namespace
{
// How much of a card the next one covers. Section 14 says "about 40% of their
// width", so what is left showing is the other 60% - which is what the fan
// steps by.
constexpr qreal overlap = 0.40;

// How far a hovered card rises, as a fraction of its height. Enough to read as
// picked out; TableScene reserves the strip for it through liftFraction().
constexpr qreal lift = 0.12;
}

qreal HandItem::liftFraction()
{
    return lift;
}

HandItem::HandItem(const CardRenderer* _renderer, QGraphicsItem* parent)
    : QGraphicsObject(parent), renderer(_renderer)
{
    setAcceptHoverEvents(true);
}

void HandItem::setHand(const std::vector<HandEntry>& hand)
{
    entries = hand;
    backs = 0;

    rebuild();
}

void HandItem::setPrompt(const PlayPrompt& prompt)
{
    if(prompt.blind)
    {
        // NOTHING BUT A COUNT, which is all a blind round sends. entries is empty
        // because the player is not entitled to see the card, and handIndex is
        // carried anyway because a position names no card - so this is the one
        // shape the fan can take without being told what it is holding.
        entries.clear();
        backs = prompt.handIndex.size();
    }
    else
    {
        entries = prompt.entries;
        backs = 0;
    }

    rebuild();
}

void HandItem::clear()
{
    entries.clear();
    backs = 0;

    rebuild();
}

void HandItem::setCardHeight(qreal height)
{
    if(qFuzzyCompare(cardHeight, height))
        return;

    cardHeight = height;

    for(Slot& slot : fan)
        slot.item->setCardHeight(cardHeight);

    layOut();
}

void HandItem::setTheme(const Theme& palette)
{
    theme = palette;

    rebuild();
}

void HandItem::setPlayable(bool value)
{
    if(playable == value)
        return;

    playable = value;

    // The lift goes with it: a hand that stops being playable while the cursor is
    // over it must not leave a card standing proud of the fan.
    if(!playable)
        hovered.reset();

    layOut();
}

std::size_t HandItem::slotCount() const
{
    return fan.size();
}

bool HandItem::isPlayable(std::size_t slot) const
{
    // THE ONE RULE, asked by the mouse and by the digit keys. Three separate
    // conditions, and leaving any of them to a caller is how the keyboard ends up
    // able to play a card the mouse refuses.
    return playable && slot < fan.size() && fan[slot].legal;
}

void HandItem::activate(std::size_t slot)
{
    if(!isPlayable(slot))
        return;

    emit cardClicked(slot);
}

void HandItem::rebuild()
{
    // DELETED, not hidden, for the reason BidStrip::rebuild() gives: the hand
    // shrinks by one card every trick, so most rebuilds are smaller than the last
    // and a hidden item still belongs to this one's children.
    for(Slot& slot : fan)
        delete slot.item;

    fan.clear();
    hovered.reset();

    const std::size_t count = backs > 0 ? backs : entries.size();

    for(std::size_t i = 0 ; i < count ; i++)
    {
        auto* item = new CardItem(renderer, this);

        if(backs > 0)
        {
            item->setBack();
        }
        else
        {
            item->setFace(entries[i].card);

            // 45% opacity and a desaturation, baked into the cached pixmap by
            // CardRenderer. An illegal card is still SHOWN - a hand with cards
            // missing from it is a different hand - it is just not for clicking.
            item->setDimmed(!entries[i].legal);
        }

        item->setCardHeight(cardHeight);

        // A BACK IS ALWAYS PLAYABLE. Both blind rounds are one-trick rounds with
        // one card and no decision, and the back is still a button - a widget
        // that drew nothing clickable here stalls the last two rounds of every
        // default game (section 17).
        fan.push_back(Slot{ item, backs > 0 || entries[i].legal });
    }

    layOut();
}

void HandItem::layOut()
{
    prepareGeometryChange();

    if(fan.empty() || cardHeight <= 0.0)
        return;

    const QSizeF card = renderer->sizeForHeight(cardHeight);

    // What each card after the first adds to the fan's width: the part of it that
    // is not covered by its neighbour.
    const qreal step = card.width() * (1.0 - overlap);

    const qreal span = card.width() + step * static_cast<qreal>(fan.size() - 1);

    // CENTRED ON THIS ITEM'S ORIGIN, which TableScene puts at the middle of the
    // reserved strip. Section 14: "the fan is centred".
    const qreal left = -span / 2.0 + card.width() / 2.0;

    for(std::size_t i = 0 ; i < fan.size() ; i++)
    {
        const bool raised = hovered && *hovered == i;

        fan[i].item->setPos(left + step * static_cast<qreal>(i),
                              raised ? -cardHeight * lift : 0.0);

        // LATER CARDS ON TOP, which is what makes the fan read left to right -
        // and it is the same ordering slotAt() walks backwards. The two have to
        // agree or the card a player points at is not the card they get.
        fan[i].item->setZValue(static_cast<qreal>(i));
    }
}

std::optional<std::size_t> HandItem::slotAt(QPointF position) const
{
    // BACKWARDS, so the topmost card wins. See the header: first-match returns
    // the card underneath the one being pointed at, and most of the fan is
    // overlap.
    for(std::size_t i = fan.size() ; i-- > 0 ; )
    {
        const CardItem* item = fan[i].item;

        if(item->boundingRect().translated(item->pos()).contains(position))
            return i;
    }

    return std::nullopt;
}

void HandItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    const std::optional<std::size_t> slot = slotAt(event->pos());

    if(!slot || !isPlayable(*slot))
    {
        // Ignored rather than accepted, so a click on a dimmed card or on the gap
        // between cards reaches whatever is behind the fan.
        event->ignore();
        return;
    }

    event->accept();

    activate(*slot);
}

void HandItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    const std::optional<std::size_t> slot = slotAt(event->pos());

    // ONLY A PLAYABLE CARD LIFTS. A dimmed card rising under the cursor says it
    // can be clicked, which is exactly what it cannot.
    const std::optional<std::size_t> next =
        slot && isPlayable(*slot) ? slot : std::nullopt;

    if(next == hovered)
        return;

    hovered = next;

    layOut();
}

void HandItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    if(!hovered)
        return;

    hovered.reset();

    layOut();
}

QRectF HandItem::boundingRect() const
{
    if(fan.empty() || cardHeight <= 0.0)
        return QRectF();

    const QSizeF card = renderer->sizeForHeight(cardHeight);

    const qreal step = card.width() * (1.0 - overlap);
    const qreal span = card.width() + step * static_cast<qreal>(fan.size() - 1);

    // TALL ENOUGH FOR A LIFTED CARD. The hover raises a card above this item's
    // origin, and a bounding rect that stopped at the unlifted top would leave
    // the raised part outside every hit test and repaint that uses it.
    return QRectF(-span / 2.0,
                  -card.height() / 2.0 - cardHeight * lift,
                  span,
                  card.height() + cardHeight * lift);
}
