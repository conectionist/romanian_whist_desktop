#include "SeatItem.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>

namespace
{

// The plaque, in card heights - which is section 18.1's rule that every metric
// on the table derives from the one unit. New art with a different card aspect
// moves the plaque with it rather than leaving it stranded at a pixel size.
constexpr qreal plaqueWidthInCards = 1.60;
constexpr qreal plaqueHeightInCards = 0.56;

// The turn ring, drawn OUTSIDE the plaque, so the bounding rect has to leave
// room for it whether it is showing or not - a seat's rect changing every time
// the turn moves would re-index the whole scene twice a card.
constexpr qreal ringWidthInCards = 0.024;
constexpr qreal ringGapInCards = 0.035;

// The little stack that says how many cards are left, rather than a number
// (section 14).
constexpr qreal stackCardWidthInCards = 0.10;
constexpr qreal stackStepInCards = 0.030;

}

SeatItem::SeatItem(QGraphicsItem* parent)
    : QGraphicsObject(parent)
{
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
}

void SeatItem::setCardHeight(qreal height)
{
    if(qFuzzyCompare(cardHeight, height))
        return;

    prepareGeometryChange();
    cardHeight = height;
    update();
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

void SeatItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(cardHeight <= 0.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing);

    const qreal width = cardHeight * plaqueWidthInCards;
    const qreal height = cardHeight * plaqueHeightInCards;
    const QRectF plaque(-width / 2.0, -height / 2.0, width, height);
    const qreal radius = height * 0.18;

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

    const qreal pad = height * 0.16;
    const QRectF inner = plaque.adjusted(pad, pad, -pad, -pad);

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
        const qreal x = inner.right() - stackWidth - (drawn - 1 - i) * step;

        painter->drawRoundedRect(QRectF(x, bottom.center().y() - stackHeight / 2.0,
                                        stackWidth, stackHeight),
                                 stackWidth * 0.2, stackWidth * 0.2);
    }
}
