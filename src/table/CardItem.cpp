#include "CardItem.h"

#include "paint/CardRenderer.h"
#include "paint/CardShadow.h"
#include "paint/DevicePixels.h"

#include <QPainter>

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
    shadowMemo.invalidate();
    update();
}

void CardItem::setPlayed(bool isWinning, const Theme::TablePalette& palette, cardshadow::Cache* cache)
{
    const cardshadow::Colours cast = cardshadow::colours(palette);

    if(played && winning == isWinning && colours == cast && shadows == cache)
        return;

    // The shadow lives outside the card, and the winner rises, so either changes
    // the bounding rect as much as a resize does.
    prepareGeometryChange();
    played = true;
    winning = isWinning;
    colours = cast;
    shadows = cache;
    shadowMemo.invalidate();
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

    const QRectF card(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height());

    if(!played)
        return card;

    // The shadow's whole reach, and the winner's lift, so the scene neither clips
    // the highlight nor leaves a trail behind a card that moves.
    return card.translated(0.0, winning ? -cardshadow::winningLift : 0.0)
        .marginsAdded(cardshadow::reach(winning));
}

void CardItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QSizeF size = cardSize();

    if(!renderer || size.isEmpty())
        return;

    // AT THE PIXELS IT WILL COVER, which is devicepixels::effectiveRatio(): the
    // device's ratio, so a window dragged to a screen with a different one
    // rasterizes again rather than scaling what it had, times the scale the view
    // draws the table at. A card 225 table units tall can land 175 pixels tall,
    // and a pixmap rasterized at 225 would be resampled down to that on its way to
    // the screen - the softness CardRenderer exists to prevent. The ratio stamped
    // on the pixmap is then below 1 on a small window, which Qt draws the same way
    // as any other.
    //
    // THE DEVICE'S RATIO, not the widget's: a scene rendered into an image has no
    // widget, and the image's own ratio is what the card has to match there.
    //
    // AND WHILE A RESIZE SETTLES, the scale it began from, which effectiveRatio()
    // answers with on its own (TableView): the pixmap the renderer already holds is
    // asked for again and drawn scaled, rather than every card being rasterized
    // afresh on every step of the drag.
    const qreal dpr = devicepixels::effectiveRatio(*painter);

    const QRectF card(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height());

    // THE WINNER RISES toward its own top, which on every seat's card points at
    // the table's centre - its shadow, glow and ring with it, as they are CSS
    // box-shadows on the element that moves.
    if(played && winning)
        painter->translate(0.0, -cardshadow::winningLift);

    // ONE SNAPPED CORNER FOR THE ART AND ITS SHADOW. Each is a picture of its own,
    // and a blit rounds its own origin; laid separately at a fraction of a pixel
    // the two could round apart, and the ring and the shadow's cut-out land a pixel
    // off the art. The shadow is a whole number of pixels from the card's corner
    // (cardshadow::render()), so both go down from the one point. A card turned by
    // other than a quarter turn is resampled anyway, and this moves it under half
    // a pixel.
    const QPointF at = devicepixels::snapped(*painter, card.topLeft());

    // Under the card: one cached picture, turned with the card by the item's
    // rotation, so the shadow falls toward the card's own bottom.
    if(played && shadows)
    {
        const cardshadow::Picture& cast = shadow(size, dpr);

        if(!cast.pixmap.isNull())
            painter->drawPixmap(at + (cast.offset - card.topLeft()), cast.pixmap);
    }

    const QSize pixels(qRound(size.width()), qRound(size.height()));

    const CardRenderer::State state =
        dimmed ? CardRenderer::State::Dimmed : CardRenderer::State::Normal;

    const QPixmap pixmap = this->card ? renderer->face(*this->card, pixels, dpr, state)
                                      : renderer->back(pixels, dpr);

    painter->drawPixmap(at, pixmap);
}

const cardshadow::Picture& CardItem::shadow(QSizeF size, qreal ratio) const
{
    return shadowMemo.picture(ratio, [&] { return shadows->picture(size, winning, colours, ratio); });
}
