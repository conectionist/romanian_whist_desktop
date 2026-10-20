#include "HandItem.h"

#include "paint/CardRenderer.h"
#include "table/CardItem.h"
#include "table/Turn.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
// THE FAN'S SHAPE (section 2.8), table.js renderHand()'s numbers, in table units.
//
// The cards step this far apart, and the fan is never wider than the second: a
// hand of eight 155-wide cards would be 743 at the full step, so it closes up to
// 80.7. Section 14 asked for an overlap of "about 40% of their width"; the mock-up's
// is about half.
constexpr qreal widestStep = 84.0;
constexpr qreal widestFan = 720.0;

// Each card drops this much times the square of how many steps it is from the
// middle, so the fan curves away toward its ends.
constexpr qreal arc = 1.6;

// Each is turned this many degrees per step from the middle, about a hinge this
// many of its own heights below its top: CSS's transform-origin: 50% 130%.
constexpr qreal turnStep = 1.9;
constexpr qreal hingeDepth = 1.3;

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

tablespace::Placement handitem::cardPlacement(std::size_t count, std::size_t slot, QSizeF card)
{
    const qreal gaps = count > 1 ? static_cast<qreal>(count - 1) : 0.0;

    // NEVER BELOW ZERO. A card wider than the whole fan would make it negative and lay
    // each card LEFT of the one before it while still drawing it on top - and
    // slotAt()'s "the last card that contains the point is the topmost" would then
    // hand a click to the card underneath. Such a card stacks the hand instead.
    const qreal step = count > 1 ? std::max(0.0, std::min(widestStep, (widestFan - card.width()) / gaps)) : 0.0;
    const qreal span = gaps * step + card.width();

    // How many steps the card is from the middle of the fan: negative to the left,
    // and a half for each of the two middle cards of an even hand.
    const qreal fromMiddle = static_cast<qreal>(slot) - gaps / 2.0;

    const qreal left = tablespace::centre.x() - span / 2.0 + static_cast<qreal>(slot) * step;
    const qreal top = tablespace::handTop + arc * fromMiddle * fromMiddle;
    const qreal degrees = turnStep * fromMiddle;

    // TURNED ABOUT THE HINGE, NOT THE CENTRE. CSS turns the card about a point below
    // it, and QGraphicsItem turns an item about its origin, which is the middle of
    // a CardItem. So the centre goes where turning about the hinge carries it - its
    // arm's length above the hinge, swung through the same angle - and the item is
    // turned there.
    const qreal radians = qDegreesToRadians(degrees);
    const qreal arm = (hingeDepth - 0.5) * card.height();
    const QPointF hinge(left + card.width() / 2.0, top + hingeDepth * card.height());

    return { hinge + QPointF(arm * std::sin(radians), -arm * std::cos(radians)), degrees };
}

HandItem::HandItem(const CardRenderer* _renderer, cardshadow::Cache* _shadows, QGraphicsItem* parent)
    : QGraphicsObject(parent), renderer(_renderer), shadows(_shadows ? _shadows : &ownShadows)
{
    setAcceptHoverEvents(true);

    // The cards draw themselves; this only places them and takes the cursor. Without
    // the flag the view still calls the empty paint() for every exposed frame, and
    // boundingRect() covers the whole fan, risen strip included.
    setFlag(QGraphicsItem::ItemHasNoContents);
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

    showLift();
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

    bounds = QRectF();

    if(fan.empty() || cardHeight <= 0.0)
        return;

    const QSizeF card = renderer->sizeForHeight(cardHeight);
    const QRectF body = cardshadow::cardRect(card);

    // A card at rest and risen: everywhere it can be hit, so everywhere this item
    // has to cover for the scene to send it the event.
    const QRectF reach = body.united(body.translated(0.0, -cardshadow::raisedLift));

    for(std::size_t i = 0 ; i < fan.size() ; i++)
    {
        CardItem* item = fan[i].item;
        const tablespace::Placement place = handitem::cardPlacement(fan.size(), i, card);

        item->setPos(place.centre);
        turnTo(*item, place.rotation);

        // LATER CARDS ON TOP, which is what makes the fan read left to right -
        // and it is the same ordering slotAt() walks backwards. The two have to
        // agree or the card a player points at is not the card they get.
        item->setZValue(static_cast<qreal>(i));

        bounds |= item->mapRectToParent(reach);
    }

    showLift();
}

void HandItem::showLift()
{
    // Every card casts the hand's shadow; the one under the cursor is raised, and
    // CardItem draws it risen and glowing. Nothing moves, so nothing here changes
    // the fan's geometry.
    for(std::size_t i = 0 ; i < fan.size() ; i++)
        fan[i].item->setHeld(hovered && *hovered == i, theme.table, shadows);
}

std::optional<std::size_t> HandItem::slotAt(QPointF position) const
{
    // BACKWARDS, so the topmost card wins. See the header: first-match returns
    // the card underneath the one being pointed at, and most of the fan is
    // overlap.
    for(std::size_t i = fan.size() ; i-- > 0 ; )
    {
        const CardItem* item = fan[i].item;
        const QRectF body = item->cardRect();
        const QPointF local = item->mapFromParent(position);

        if(body.contains(local))
            return i;

        // AND WHERE THE RAISED CARD HAS RISEN TO - after its own resting place but
        // before the card beneath it, as the card is drawn. The card above it, which
        // was tested first, still wins where it overlaps the risen part: the
        // mock-up's cards keep their order when one rises.
        if(hovered && *hovered == i && body.translated(0.0, -cardshadow::raisedLift).contains(local))
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

    showLift();
}

void HandItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    if(!hovered)
        return;

    hovered.reset();

    showLift();
}

QRectF HandItem::boundingRect() const
{
    // EVERY CARD, AT REST AND RISEN, turned as it is - layOut() works it out once,
    // since the cards are where they are until the hand changes. It is what the
    // scene tests before it sends this item a press or a hover at all, and it
    // includes the risen part, so a raised card is hit there too. The shadows are
    // each card's own to repaint.
    return bounds;
}
