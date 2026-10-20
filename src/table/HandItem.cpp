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
// picked out; boundingRect() includes it, so the raised part is still hit.
constexpr qreal lift = 0.12;

// WHETHER TWO HANDS WOULD FAN THE SAME, which is the only question setHand()
// asks before deciding to do nothing.
//
// `legal` IS COMPARED along with the card even though a snapshot's is always
// true, because what it is compared against may still be the last PROMPT's
// entries - the fan is not rebuilt when the turn ends, only when the next
// snapshot lands. That is exactly the case where the dimming has to come off,
// and comparing cards alone would leave it on.
//
// A local function rather than an operator== on HandEntry: this is the fan
// asking whether it would draw its own children differently, not a claim about
// when two hands are the same hand.
bool sameEntries(const std::vector<HandEntry>& left, const std::vector<HandEntry>& right)
{
    return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                      [](const HandEntry& a, const HandEntry& b)
                      { return a.card == b.card && a.legal == b.legal; });
}
}

HandItem::HandItem(const CardRenderer* _renderer, QGraphicsItem* parent)
    : QGraphicsObject(parent), renderer(_renderer)
{
    setAcceptHoverEvents(true);
}

void HandItem::setHand(const std::vector<HandEntry>& hand)
{
    // THE PROMPT OUTRANKS THE SNAPSHOT while it is live, and this line is the
    // whole of what stops a relayout from hanging the game. TableScene::relayout()
    // re-sources the fan from the snapshot, and it runs on every theme change -
    // including while the engine is parked in playCard() waiting for this fan to
    // be clicked. Until the table redesign's phase 2 it ran on every viewport
    // resize as well, which is how this was found.
    //
    // In a Forehead or Hidden round the snapshot's hand is EMPTY by design, so
    // taking it here would leave no card to click, no digit key in range, and
    // nothing anywhere that could answer the prompt: the game stops for good. In
    // an ordinary round it costs less and is still wrong - the legality lighting
    // goes, illegal cards undim and lift under the cursor, and the click they now
    // invite is swallowed by MainWindow::onCardPicked().
    if(source == Source::Prompt)
        return;

    // NOTHING TO DO IF NOTHING CHANGED. A game publishes about 1300 snapshots and
    // every theme change arrives here as well; rebuilding deletes and re-news
    // every CardItem in the fan, which is the churn TableScene's class comment
    // rules out for the seats and the trick - and, from 18.2 on, a card deleted
    // out from under the animation moving it.
    //
    // `backs` is part of the question: a blind round's fan is backs against an
    // empty `entries`, and the snapshot hand that follows it is empty too, so the
    // entries alone would compare equal and leave the backs standing.
    if(backs == 0 && sameEntries(entries, hand))
        return;

    entries = hand;
    backs = 0;

    rebuild();
}

void HandItem::setPrompt(const PlayPrompt& prompt)
{
    // THE FAN IS THE PROMPT'S NOW, until setPlayable(false) hands it back. See
    // setHand() for what that is worth.
    source = Source::Prompt;

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
    source = Source::Snapshot;

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
    // BEFORE THE GUARD BELOW, and deliberately: setPrompt() takes the fan and the
    // caller arms it a line later, so there is a moment where the prompt owns a
    // fan that is not yet playable. Ending the claim only on a CHANGE to false
    // would step over that moment and leave the prompt owning the fan past the
    // end of its own turn - a stale hand that no snapshot can redraw.
    if(!value)
        source = Source::Snapshot;

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

            // Darkened and dulled, baked into the cached pixmap by
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

    // CENTRED ON THIS ITEM'S ORIGIN, which TableScene puts on the table's centre
    // line, half a card below section 2.8's hand top. Section 14: "the fan is
    // centred".
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
