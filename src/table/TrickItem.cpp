#include "TrickItem.h"

#include "table/CardItem.h"
#include "table/RackItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"
#include "table/Turn.h"

#include <QVariantAnimation>

namespace
{
// table.js animateIn()'s starting looks: a bot's card leaves its owner's panel small
// and faint, and yours leaves the fan at the fan's size, a little larger than it
// rests at - the hand's cards are 225 tall and the felt's 205.2 or 181.2.
constexpr qreal botFlightScale = 0.55;
constexpr qreal botFlightOpacity = 0.3;

// table.js sweep(): each card ends at the winner's panel at a third of its size, gone.
constexpr qreal sweepScale = 0.35;

QPointF lerp(QPointF from, QPointF to, qreal t)
{
    return from + (to - from) * t;
}

qreal lerp(qreal from, qreal to, qreal t)
{
    return from + (to - from) * t;
}
}

tablespace::Placement trickitem::cardPlacement(unsigned int seatCount, const seatlayout::Place& place)
{
    const qreal height = tablespace::playedCardHeight(seatCount);

    if(place.human)
    {
        return tablespace::Placement{ QPointF(tablespace::centre.x(),
                                              tablespace::handTop - tablespace::playedInset - height / 2.0),
                                      0.0 };
    }

    // The felt's edge, the inset, then half the card: table.js trickPos().
    const qreal radius = tablespace::feltRadius - tablespace::playedInset - height / 2.0;

    return tablespace::Placement{ seatlayout::onCircle(*place.angle, radius),
                                  rackitem::frameRotation(*place.angle) };
}

std::optional<tablespace::Placement> trickitem::cardPlacement(unsigned int seatCount, int humanSeat,
                                                              unsigned int seat)
{
    const std::optional<seatlayout::Place> place = seatlayout::place(seatCount, humanSeat, seat);

    if(!place)
        return std::nullopt;

    return cardPlacement(seatCount, *place);
}

tablespace::Placement trickitem::leadMarkPlacement(const seatlayout::Place& place)
{
    if(place.human)
        return tablespace::Placement{ QPointF(tablespace::centre.x(), tablespace::leadMarkHumanY), 0.0 };

    return tablespace::Placement{ seatlayout::onCircle(*place.angle, tablespace::goldLineRadius),
                                  rackitem::frameRotation(*place.angle) };
}

std::optional<tablespace::Placement> trickitem::leadMarkPlacement(unsigned int seatCount, int humanSeat,
                                                                  unsigned int seat)
{
    const std::optional<seatlayout::Place> place = seatlayout::place(seatCount, humanSeat, seat);

    if(!place)
        return std::nullopt;

    return leadMarkPlacement(*place);
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
    {
        const std::size_t own = cards.size();

        cards.push_back(new CardItem(renderer, this));

        // ITS OWN CLOCK, made once. Capturing the index and not a pointer into
        // `tweens`, which grows.
        Tween tween;
        tween.clock = new QVariantAnimation(this);
        tween.clock->setStartValue(0.0);
        tween.clock->setEndValue(1.0);

        connect(tween.clock, &QVariantAnimation::valueChanged, this,
                [this, own](const QVariant& value) { step(own, value.toReal()); });

        // STOPPED, however it stopped: it ran out, or a frame cut it short, or a test
        // set its time to the end. finish() is safe to reach twice.
        connect(tween.clock, &QAbstractAnimation::stateChanged, this,
                [this, own](QAbstractAnimation::State now, QAbstractAnimation::State)
                {
                    if(now == QAbstractAnimation::Stopped)
                        finish(own);
                });

        tweens.push_back(tween);
    }

    return cards[index];
}

void TrickItem::setPace(motion::Pace pace)
{
    currentPace = pace;

    // Nothing flies at Off, and what was in the air lands where it was going.
    if(!motion::animates(pace))
        finishAll();
}

QVariantAnimation* TrickItem::flight(std::size_t index) const
{
    return index < tweens.size() && tweens[index].active ? tweens[index].clock : nullptr;
}

bool TrickItem::isFlying() const
{
    for(const Tween& tween : tweens)
    {
        if(tween.active)
            return true;
    }

    return false;
}

TrickItem::Arrivals TrickItem::arrivalsFor(const GameSnapshot& snapshot) const
{
    // WITH NOTHING TO FLY FROM or nothing to fly with: the first frame, a pace of
    // Off. Every card is placed at once.
    if(!seen || !motion::animates(currentPace))
        return {};

    // A new round's table is a new table, whatever it holds.
    if(snapshot.roundNumber != shownRound || snapshot.table.size() < shown.size())
        return {};

    for(std::size_t i = 0 ; i < shown.size() ; i++)
    {
        if(snapshot.table[i].seatIndex != shown[i].seat || !(snapshot.table[i].card == shown[i].card))
            return {};
    }

    return { true, shown.size() };
}

void TrickItem::launch(std::size_t index, const seatlayout::Place& seat, const tablespace::Placement& place,
                       qreal cardHeight, const romanian_whist::Card& card, const HandPlace& fromHand)
{
    // WHATEVER THIS CARD WAS DOING ENDS FIRST - a sweep carrying it off, most of all,
    // since the first card of a trick reuses the first card of the last - and on its
    // own terms, before this flight's are written over them.
    finish(index);

    Tween& tween = tweens[index];

    tween.to = place;
    tween.toScale = 1.0;
    tween.toOpacity = 1.0;
    tween.leaving = false;

    // YOURS FROM THE FAN where it stood, when the fan can say; every other card from
    // its owner's panel, turned as it will lie, so it only has to travel.
    const std::optional<tablespace::Placement> departure =
        seat.human && fromHand ? fromHand(card) : std::nullopt;

    if(departure)
    {
        tween.from = *departure;
        tween.fromScale = tablespace::handCardHeight / cardHeight;
        tween.fromOpacity = 1.0;
        tween.fromHand = true;
    }
    else
    {
        tween.from = { seat.panel.center(), place.rotation };
        tween.fromScale = botFlightScale;
        tween.fromOpacity = botFlightOpacity;
        tween.fromHand = false;
    }

    tween.active = true;

    tween.clock->stop();
    tween.clock->setDuration(motion::flightDuration(currentPace));
    tween.clock->setEasingCurve(motion::flightCurve());

    // At the start BEFORE the first frame is painted, rather than at the end for one
    // frame and then jumping back: start() reports the start value, which does the
    // same, but a clock that never ticks (a test's) must not leave the card at rest.
    step(index, 0.0);

    updateStacking();

    tween.clock->start();
}

void TrickItem::step(std::size_t index, qreal progress)
{
    const Tween& tween = tweens[index];

    if(!tween.active)
        return;

    CardItem* item = cards[index];

    item->setPos(lerp(tween.from.centre, tween.to.centre, progress));
    turnTo(*item, lerp(tween.from.rotation, tween.to.rotation, progress));
    scaleTo(*item, lerp(tween.fromScale, tween.toScale, progress));
    item->setOpacity(lerp(tween.fromOpacity, tween.toOpacity, progress));
}

void TrickItem::finish(std::size_t index)
{
    if(index >= tweens.size())
        return;

    Tween& tween = tweens[index];

    // BEFORE the stop below, which reports it stopped and comes back here.
    if(!tween.active)
        return;

    tween.active = false;
    tween.clock->stop();

    CardItem* item = cards[index];

    item->setPos(tween.to.centre);
    turnTo(*item, tween.to.rotation);
    scaleTo(*item, 1.0);
    item->setOpacity(1.0);

    // A SWEPT CARD IS GONE when it gets there; the item waits for the next trick.
    if(tween.leaving)
    {
        tween.leaving = false;
        item->hide();
    }

    updateStacking();
}

void TrickItem::finishLeaving()
{
    for(std::size_t i = 0 ; i < tweens.size() ; i++)
    {
        if(tweens[i].active && tweens[i].leaving)
            finish(i);
    }
}

std::optional<unsigned int> TrickItem::sweepWinner(const GameSnapshot& snapshot) const
{
    // THE FRAME AFTER A FULL TRICK, in its round, with the felt bare: onTrickWon()'s
    // frame, held for the dwell, then the next trick's first. A round's last trick is
    // followed by the next round, which this leaves alone.
    if(!seen || !motion::animates(currentPace) || snapshot.roundNumber != shownRound)
        return std::nullopt;

    if(!snapshot.table.empty() || shown.empty() || shown.size() != snapshot.seats.size())
        return std::nullopt;

    // THE ENGINE'S WINNER, as the full trick's frame ranked it, never recomputed.
    for(const Shown& card : shown)
    {
        if(card.winning)
            return card.seat;
    }

    return std::nullopt;
}

void TrickItem::sweep(const seatlayout::Place& winner)
{
    for(std::size_t i = 0 ; i < shown.size() && i < cards.size() ; i++)
    {
        // A card still landing lands first, so the sweep starts from the felt.
        finish(i);

        CardItem* item = cards[i];

        if(!item->isVisible())
            continue;

        Tween& tween = tweens[i];

        tween.from = { item->pos(), item->rotation() };
        tween.to = { winner.panel.center(), item->rotation() };
        tween.fromScale = 1.0;
        tween.fromOpacity = 1.0;
        tween.toScale = sweepScale;
        tween.toOpacity = 0.0;
        tween.fromHand = false;
        tween.leaving = true;
        tween.active = true;

        tween.clock->setDuration(motion::sweepDuration(currentPace));
        tween.clock->setEasingCurve(motion::sweepCurve());

        step(i, 0.0);
        tween.clock->start();
    }

    updateStacking();
}

void TrickItem::finishAll()
{
    for(std::size_t i = 0 ; i < tweens.size() ; i++)
        finish(i);
}

void TrickItem::updateStacking()
{
    bool leavingYourHand = false;

    for(const Tween& tween : tweens)
        leavingYourHand = leavingYourHand || (tween.active && tween.fromHand);

    setZValue(leavingYourHand ? tablespace::z::flyingCard : tablespace::z::playedCards);
}

void TrickItem::setTrick(const GameSnapshot& snapshot, const seatlayout::Places& places, const Theme& theme,
                         const HandPlace& fromHand)
{
    const unsigned int seatCount = static_cast<unsigned int>(snapshot.seats.size());
    const qreal cardHeight = tablespace::playedCardHeight(seatCount);

    // A WINNER ONCE THERE IS SOMETHING TO BEAT. The engine ranks a lone first card
    // as winning, which it is; table.js highlights from the second card on, since
    // until then the highlight says nothing the card does not.
    const bool contested = snapshot.table.size() >= 2;

    // WHICH CARDS ARE NEW, and whether the flights already in the air still belong:
    // when this frame does not follow the last, whatever was moving lands where it
    // was going before anything is placed.
    const Arrivals arrivals = arrivalsFor(snapshot);
    const std::optional<unsigned int> winner = sweepWinner(snapshot);

    // A CARD ON THE FELT ENDS ANY SWEEP: the next trick has begun.
    if(!snapshot.table.empty())
        finishLeaving();

    if(!arrivals.agrees)
        finishAll();

    if(winner && *winner < places.size() && places[*winner])
        sweep(*places[*winner]);

    for(std::size_t i = 0 ; i < snapshot.table.size() ; i++)
    {
        const PlayedCardView& played = snapshot.table[i];

        // Read off the entry, never derived from leadSeat and an offset - see
        // the header. A seat this table cannot place is a snapshot this scene
        // cannot draw, and leaving it off is better than inventing a place.
        const std::optional<seatlayout::Place>* seat =
            played.seatIndex < places.size() ? &places[played.seatIndex] : nullptr;

        if(!seat || !*seat)
        {
            // HIDDEN, not merely skipped. The item at this index may already be
            // on the table from a previous frame, and the hide pass below starts
            // past the end of THIS table - so an index inside it is one nothing
            // else reaches, and a bare `continue` leaves the last frame's card
            // sitting there with its face, its turn and its winning highlight.
            cardAt(i);
            finish(i);
            cards[i]->hide();
            continue;
        }

        CardItem* item = cardAt(i);

        item->setCardHeight(cardHeight);
        item->setFace(played.card);
        const tablespace::Placement place = trickitem::cardPlacement(seatCount, **seat);

        if(arrivals.agrees && i >= arrivals.first)
        {
            launch(i, **seat, place, cardHeight, played.card, fromHand);
        }
        else if(tweens[i].active)
        {
            // STILL IN THE AIR: the same card, so only where it is going can change.
            tweens[i].to = place;
        }
        else
        {
            item->setPos(place.centre);
            turnTo(*item, place.rotation);
            scaleTo(*item, 1.0);
            item->setOpacity(1.0);
        }

        // THE HIGHLIGHT IS isWinning AND IS NEVER RECOMPUTED. It is the engine's
        // own ranking of a partly played trick, filled from
        // getCurrentTrickLeader(), so it cannot disagree with the winner the
        // engine eventually declares.
        item->setPlayed(contested && played.isWinning, theme.table, shadows);

        item->setZValue(static_cast<qreal>(i));
        item->show();
    }

    // Hidden rather than deleted, for the reason cardAt() gives - BUT A CARD BEING
    // SWEPT OFF is the sweep's to hide, when it gets there: the frames that follow
    // the clearing one show the same bare felt, and must not cut it short.
    for(std::size_t i = snapshot.table.size() ; i < cards.size() ; i++)
    {
        if(tweens[i].active && tweens[i].leaving)
            continue;

        finish(i);
        cards[i]->hide();
    }

    shown.clear();

    for(const PlayedCardView& played : snapshot.table)
        shown.push_back({ played.seatIndex, played.card, played.isWinning });

    shownRound = snapshot.roundNumber;
    seen = true;
}

QRectF TrickItem::boundingRect() const
{
    // The cards are children and carry their own rects; this paints nothing.
    return QRectF();
}

void TrickItem::paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*)
{
}
