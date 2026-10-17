#include "CardItem.h"

#include "paint/CardRenderer.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

namespace
{

// How far the winning card's shadow and hairline reach past the card, as a
// fraction of the card's height. boundingRect() has to include it or the scene
// clips the highlight to the card and leaves a trail behind a moving one.
constexpr qreal highlightMargin = 0.06;

}

CardItem::CardItem(const CardRenderer* renderer, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , renderer(renderer)
{
}

void CardItem::setFace(romanian_whist::Card face)
{
    if(card && *card == face)
        return;

    card = face;
    update();
}

void CardItem::setBack()
{
    if(!card)
        return;

    card.reset();
    update();
}

void CardItem::setCardHeight(qreal height)
{
    if(qFuzzyCompare(cardHeight, height))
        return;

    // AROUND the change, not after it. The scene indexes items by their bounding
    // rect, and a rect that changes without this is a stale entry: the old area
    // is never repainted and the new one may never be visited.
    prepareGeometryChange();
    cardHeight = height;
    update();
}

void CardItem::setWinning(bool isWinning, QColor accentColour)
{
    if(winning == isWinning && accent == accentColour)
        return;

    // The highlight lives outside the card, so its arrival and departure change
    // the bounding rect as much as a resize does.
    prepareGeometryChange();
    winning = isWinning;
    accent = accentColour;
    update();
}

void CardItem::setDimmed(bool isDimmed)
{
    if(dimmed == isDimmed)
        return;

    dimmed = isDimmed;
    update();
}

QSizeF CardItem::cardSize() const
{
    if(!renderer || cardHeight <= 0.0)
        return QSizeF();

    return renderer->sizeForHeight(cardHeight);
}

QRectF CardItem::boundingRect() const
{
    const QSizeF size = cardSize();

    if(size.isEmpty())
        return QRectF();

    const qreal margin = winning ? cardHeight * highlightMargin : 0.0;

    return QRectF(-size.width() / 2.0 - margin,
                  -size.height() / 2.0 - margin,
                  size.width() + margin * 2.0,
                  size.height() + margin * 2.0);
}

void CardItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget* widget)
{
    const QSizeF size = cardSize();

    if(!renderer || size.isEmpty())
        return;

    // Off the widget being painted into, which is the viewport - so a window
    // dragged to a screen with a different ratio rasterizes again at the new one
    // rather than scaling what it had. `widget` is null when a scene is rendered
    // somewhere other than a view, and 1.0 is the honest answer there.
    //
    // TIMES THE SCALE THE CARD IS DRAWN AT, which is the table redesign's phase 2.
    // The view scales a fixed table space to the window now, so a card 225 table
    // units tall can land 175 pixels tall - and a pixmap rasterized at 225 would
    // be resampled down to that on its way to the screen, which is the softness
    // CardRenderer exists to prevent. Asking for the size it will really occupy
    // makes the blit 1:1 again; the ratio stamped on the pixmap is then below 1 on
    // a small window, which Qt draws the same way as any other.
    //
    // levelOfDetailFromTransform() is the transform's scale, rotation or not, so a
    // tilted trick card asks for the same pixels as a straight one.
    const qreal dpr = (widget ? widget->devicePixelRatioF() : 1.0)
                    * QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());

    const QRectF card(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height());

    if(winning)
    {
        // A raised shadow and a thin accent hairline (section 18). Drawn before
        // the card so the shadow sits under it rather than over its border.
        const qreal margin = cardHeight * highlightMargin;

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 70));
        painter->drawRoundedRect(card.adjusted(-margin, -margin, margin, margin),
                                 margin, margin);
    }

    const QSize pixels(qRound(size.width()), qRound(size.height()));

    const CardRenderer::State state =
        dimmed ? CardRenderer::State::Dimmed : CardRenderer::State::Normal;

    const QPixmap pixmap = this->card ? renderer->face(*this->card, pixels, dpr, state)
                                      : renderer->back(pixels, dpr);

    painter->drawPixmap(card.topLeft(), pixmap);

    if(winning)
    {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, std::max(1.0, cardHeight * 0.016)));
        painter->drawRoundedRect(card, cardHeight * 0.04, cardHeight * 0.04);
    }
}
