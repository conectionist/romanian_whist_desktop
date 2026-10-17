#include "SeatItem.h"

#include "paint/CardRenderer.h"
#include "table/CardItem.h"

#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace
{

// The plaque, in multiples of its unit (setCardHeight()), so every metric on it
// scales together. On the table that unit is TableScene's fixed plaqueUnit, in
// table units; until the table redesign's phase 2 it was the hand's card height,
// derived from the window.
constexpr qreal plaqueWidthInCards = 1.60;
constexpr qreal plaqueHeightInCards = 0.56;

// The plaque's corner radius, as a fraction of its height. Shared by paint() and
// shape(), so what blocks the mouse is exactly what is drawn.
constexpr qreal cornerInPlaques = 0.18;

// The turn ring, drawn OUTSIDE the plaque, so the bounding rect has to leave
// room for it whether it is showing or not - a seat's rect changing every time
// the turn moves would re-index the whole scene twice a card.
constexpr qreal ringWidthInCards = 0.024;
constexpr qreal ringGapInCards = 0.035;

// The little stack that says how many cards are left, rather than a number
// (section 14).
constexpr qreal stackCardWidthInCards = 0.10;
constexpr qreal stackStepInCards = 0.030;

// How much of the plaque's HEIGHT a face-up card takes, leaving a hair of margin
// top and bottom. Of the plaque and not of the padded inner box, which is only
// 0.38 card-heights - a card that fitted inside that would be too small to read
// a rank off, and reading it is the whole point of section 17.
constexpr qreal faceHeightInPlaques = 0.88;

// The gap between that card and the text beside it, as a fraction of the card's
// own width.
constexpr qreal faceGapInCards = 0.22;

}

qreal seatitem::faceHeightFor(qreal cardHeight)
{
    return cardHeight * plaqueHeightInCards * faceHeightInPlaques;
}

seatitem::Layout seatitem::layoutFor(qreal cardHeight, QSizeF faceSize)
{
    Layout layout;

    if(cardHeight <= 0.0)
        return layout;

    const qreal width = cardHeight * plaqueWidthInCards;
    const qreal height = cardHeight * plaqueHeightInCards;

    layout.plaque = QRectF(-width / 2.0, -height / 2.0, width, height);

    const qreal pad = height * 0.16;
    const QRectF inner = layout.plaque.adjusted(pad, pad, -pad, -pad);

    // The stack's right edge is the inner box's, whether or not it is the thing
    // being drawn - so the two never disagree about where this side of the
    // plaque ends.
    layout.stack = inner;

    if(faceSize.isEmpty())
    {
        // No card: the text gets the whole inner box, exactly as it did before
        // C4.
        layout.text = inner;

        return layout;
    }

    // CENTRED IN THE PLAQUE, not in the inner box, because it is taller than the
    // inner box - see faceHeightInPlaques.
    layout.card = QRectF(layout.plaque.right() - pad - faceSize.width(),
                         layout.plaque.center().y() - faceSize.height() / 2.0,
                         faceSize.width(),
                         faceSize.height());

    // AND THE TEXT GIVES UP THAT WIDTH. The name is elided against this rect, so
    // a text column that did not narrow here would be measured at one width and
    // drawn at another - the defect B2b found four times over.
    const qreal taken = faceSize.width() + faceSize.width() * faceGapInCards;

    layout.text = inner.adjusted(0.0, 0.0, -taken, 0.0);

    return layout;
}

SeatItem::SeatItem(const CardRenderer* _renderer, QGraphicsItem* parent)
    : QGraphicsObject(parent), renderer(_renderer)
{
    face = new CardItem(renderer, this);
    face->hide();

    // YOUR PLAQUE COVERS THE BOTTOM OF YOUR HAND (section 2.8), and Qt sends hover
    // only to the topmost item that asks for it. Without this the hand is that
    // item, and lifts a card the plaque is hiding. See mousePressEvent() for the
    // other half.
    setAcceptHoverEvents(true);
}

QSizeF SeatItem::plaqueSize(qreal cardHeight)
{
    if(cardHeight <= 0.0)
        return QSizeF();

    const qreal margin = cardHeight * (ringWidthInCards + ringGapInCards) * 2.0;

    return QSizeF(cardHeight * plaqueWidthInCards + margin * 2.0,
                  cardHeight * plaqueHeightInCards + margin * 2.0);
}

void SeatItem::setSeat(const SeatView& view, bool isActive, const Theme& palette)
{
    seat = view;
    active = isActive;
    theme = palette;
    update();

    refreshFace();
}

void SeatItem::setCardHeight(qreal height)
{
    if(qFuzzyCompare(cardHeight, height))
        return;

    prepareGeometryChange();
    cardHeight = height;
    update();

    refreshFace();
}

// NO BID IS NOT A BID OF ZERO. hasBet is on the snapshot precisely to tell them
// apart, and reading `bet` without it makes every seat look like it bid nothing
// and is exactly on target for the whole of the bidding.
SeatItem::Pill SeatItem::pillState() const
{
    if(!seat.hasBet)
        return Pill::NoBid;

    if(seat.tricksWon == seat.bet)
        return Pill::OnTarget;

    return seat.tricksWon < seat.bet ? Pill::Owing : Pill::Over;
}

QColor SeatItem::pillColour() const
{
    switch(pillState())
    {
    case Pill::NoBid:    return theme.textPrimary;
    case Pill::OnTarget: return theme.bidOnTarget;
    case Pill::Owing:    return theme.bidOwing;
    case Pill::Over:     return theme.bidOver;
    }

    return theme.textPrimary;
}

// The plaque plus the ring, centred on the origin - so it is exactly what
// plaqueSize() reports, and the layout's inset and the scene's repaint region
// cannot drift apart.
QRectF SeatItem::boundingRect() const
{
    const QSizeF size = plaqueSize(cardHeight);

    if(size.isEmpty())
        return QRectF();

    return QRectF(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height());
}

// THE PLAQUE AS DRAWN, not the bounding rect. The rect leaves room for the turn
// ring whether it is showing or not, and in that margin the card under the plaque
// is still visible - so it has to stay clickable there.
QPainterPath SeatItem::shape() const
{
    QPainterPath path;

    if(cardHeight <= 0.0)
        return path;

    const qreal radius = cardHeight * plaqueHeightInCards * cornerInPlaques;

    path.addRoundedRect(layoutFor().plaque, radius, radius);

    return path;
}

// TAKEN, AND NOTHING DONE WITH IT. QGraphicsItem's own handler ignores a press on
// an item that is neither movable nor selectable, and an ignored press goes on to
// the item beneath: your own plaque let a click on your name through to the card
// under it, and played that card.
void SeatItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

bool SeatItem::showsFace() const
{
    // NOT "is this a Forehead round". The snapshot does not need to be asked:
    // refreshFromEngine() fills this hand only where canSeeHand(viewer, holder)
    // allows, so a non-empty hand IS the permission - and a spectator game fills
    // none at all, which is why a spectated Forehead round shows no faces.
    return !seat.hand.empty();
}

seatitem::Layout SeatItem::layoutFor() const
{
    if(!showsFace() || !renderer)
        return seatitem::layoutFor(cardHeight, QSizeF());

    return seatitem::layoutFor(cardHeight,
                               renderer->sizeForHeight(seatitem::faceHeightFor(cardHeight)));
}

void SeatItem::refreshFace()
{
    const seatitem::Layout layout = layoutFor();

    // BOTH, and the hand is asked FIRST. The layout's card rect is derived from
    // showsFace(), so today the two cannot disagree - and the day they did, the
    // front() below would be a read off an empty vector rather than a plaque
    // drawn slightly wrong. Guarding on the thing actually dereferenced is what
    // makes that a hidden card instead of undefined behaviour.
    if(seat.hand.empty() || layout.card.isEmpty())
    {
        face->hide();
        return;
    }

    // THE FIRST CARD AND ONLY THE FIRST. A Forehead round is a one-trick round,
    // so a visible hand is one card - and the plaque has room for one. Drawing a
    // hand of several here would be inventing a layout for a state the engine's
    // own schedule cannot produce.
    face->setFace(seat.hand.front().card);
    face->setCardHeight(seatitem::faceHeightFor(cardHeight));

    // CardItem's origin is its centre, which is what these two lines are about.
    face->setPos(layout.card.center());
    face->show();
}

void SeatItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(cardHeight <= 0.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing);

    // ONE ANSWER ABOUT WHERE THINGS GO, shared with refreshFace() so the card the
    // child is placed at and the width the text is measured against cannot
    // disagree.
    const seatitem::Layout layout = layoutFor();

    const qreal height = cardHeight * plaqueHeightInCards;
    const QRectF plaque = layout.plaque;
    const qreal radius = height * cornerInPlaques;

    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.seatPlaque);
    painter->drawRoundedRect(plaque, radius, radius);

    // THE TURN RING, and never a colour-only cue: section 18 requires a shape
    // cue beside it, which is the marker drawn against the name below.
    if(active)
    {
        const qreal gap = cardHeight * ringGapInCards;
        const qreal pen = cardHeight * ringWidthInCards;

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(theme.accent, pen));
        painter->drawRoundedRect(plaque.adjusted(-gap, -gap, gap, gap),
                                 radius + gap, radius + gap);
    }

    const QRectF inner = layout.text;

    // THE FLOORS HERE ARE IN TABLE UNITS, so they bind only at a unit far below
    // TableScene's and promise nothing on screen. What keeps the name readable
    // there is tablespace::minimumScale, which the window cannot shrink past.
    QFont nameFont = painter->font();
    nameFont.setPointSizeF(std::max(6.0, height * 0.26));
    nameFont.setBold(true);

    // A name is arbitrary text from a setup dialog, so it is elided rather than
    // allowed to run past the plaque - which is what a six-seat table at a small
    // window size would otherwise do.
    const QString marker = active ? QStringLiteral("▸ ") : QString();
    const QString suffix = seat.isHuman ? tr(" (you)") : QString();
    const QString name = marker + QString::fromStdString(seat.name) + suffix;

    painter->setFont(nameFont);
    painter->setPen(theme.textPrimary);

    const QRectF nameRect(inner.left(), inner.top(), inner.width(), inner.height() * 0.5);

    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetricsF(nameFont).elidedText(name, Qt::ElideRight, nameRect.width()));

    // The pill: "bid - won" as a compact pair, so a player never subtracts.
    QFont pillFont = painter->font();
    pillFont.setPointSizeF(std::max(6.0, height * 0.24));
    pillFont.setBold(true);

    const QRectF bottom(inner.left(), inner.center().y(), inner.width(), inner.height() * 0.5);

    painter->setFont(pillFont);
    painter->setPen(pillColour());

    const QString pill = seat.hasBet
                             ? tr("%1 · %2").arg(seat.bet).arg(seat.tricksWon)
                             : tr("no bid");

    painter->drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter, pill);

    // THE STACK, OR NOTHING. A seat showing a face-up card is showing what it
    // holds already, and drawing the count beside it would be the same fact
    // twice - in a Forehead round it is always exactly one.
    if(!layout.card.isEmpty())
        return;

    // Cards left, as a tiny stack rather than a number (section 14). Drawn from
    // the right so it grows toward the middle and never collides with the name.
    const qreal stackWidth = cardHeight * stackCardWidthInCards;
    const qreal stackHeight = stackWidth * 1.45;
    const qreal step = cardHeight * stackStepInCards;

    // Capped at what the plaque can hold. Eight is the most a round ever deals,
    // so this is a guard rather than a behaviour anyone will see.
    const unsigned int drawn = std::min(seat.cardsLeft, 8u);

    painter->setPen(QPen(theme.textPrimary, std::max(1.0, cardHeight * 0.006)));
    painter->setBrush(theme.tableSurface);

    for(unsigned int i = 0 ; i < drawn ; i++)
    {
        const qreal x = layout.stack.right() - stackWidth - (drawn - 1 - i) * step;

        painter->drawRoundedRect(QRectF(x, bottom.center().y() - stackHeight / 2.0,
                                        stackWidth, stackHeight),
                                 stackWidth * 0.2, stackWidth * 0.2);
    }
}
