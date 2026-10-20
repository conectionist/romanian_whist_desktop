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
    setCast(isWinning ? cardshadow::Look::Winning : cardshadow::Look::Played, palette, cache);
}

void CardItem::setHeld(bool raised, const Theme::TablePalette& palette, cardshadow::Cache* cache)
{
    setCast(raised ? cardshadow::Look::Raised : cardshadow::Look::Held, palette, cache);
}

void CardItem::setCast(cardshadow::Look look, const Theme::TablePalette& palette, cardshadow::Cache* cache)
{
    const cardshadow::Colours wanted = cardshadow::colours(palette);

    if(cast == look && colours == wanted && shadows == cache)
        return;

    // The shadow lives outside the card, and the winner and a raised card rise, so
    // any of these changes the bounding rect as much as a resize does.
    prepareGeometryChange();
    cast = look;
    colours = wanted;
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

QRectF CardItem::cardRect() const
{
    return cardshadow::cardRect(cardSize());
}

QRectF CardItem::boundingRect() const
{
    if(cardSize().isEmpty())
        return QRectF();

    const QRectF body = cardRect();

    if(!cast)
        return body;

    // The shadow's whole reach, and the rise, so the scene neither clips the
    // highlight nor leaves a trail behind a card that moves.
    return body.translated(0.0, -cardshadow::lift(*cast)).marginsAdded(cardshadow::reach(*cast));
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

    const QRectF body = cardRect();

    // THE WINNER RISES toward its own top, which on every seat's card points at
    // the table's centre, and so does a raised card in your hand, along its tilt -
    // the shadow, glow and ring with them, as they are CSS box-shadows on the
    // element that moves.
    if(cast)
        painter->translate(0.0, -cardshadow::lift(*cast));

    // ONE SNAPPED CORNER FOR THE ART AND ITS SHADOW. Each is a picture of its own,
    // and a blit rounds its own origin; laid separately at a fraction of a pixel
    // the two could round apart, and the ring and the shadow's cut-out land a pixel
    // off the art. The shadow is a whole number of pixels from the card's corner
    // (cardshadow::render()), so both go down from the one point. A card turned by
    // other than a quarter turn is resampled anyway, and this moves it under half
    // a pixel.
    const QPointF at = devicepixels::snapped(*painter, body.topLeft());

    // Under the card: one cached picture, turned with the card by the item's
    // rotation, so the shadow falls toward the card's own bottom.
    if(cast && shadows)
    {
        const cardshadow::Picture& picture = shadow(size, dpr);

        if(!picture.pixmap.isNull())
            painter->drawPixmap(at + (picture.offset - body.topLeft()), picture.pixmap);
    }

    const QSize pixels(qRound(size.width()), qRound(size.height()));

    const CardRenderer::State state =
        dimmed ? CardRenderer::State::Dimmed : CardRenderer::State::Normal;

    const QPixmap pixmap = card ? renderer->face(*card, pixels, dpr, state)
                                : renderer->back(pixels, dpr);

    painter->drawPixmap(at, pixmap);
}

const cardshadow::Picture& CardItem::shadow(QSizeF size, qreal ratio) const
{
    return shadowMemo.picture(ratio, [&] { return shadows->picture(size, *cast, colours, ratio); });
}
