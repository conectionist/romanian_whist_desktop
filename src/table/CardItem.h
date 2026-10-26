#ifndef CARD_ITEM_H
#define CARD_ITEM_H

#include "paint/CardShadow.h"
#include "paint/PictureMemo.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QGraphicsObject>

#include <optional>

class CardRenderer;

// One card on the table, face up or face down.
//
// A QGraphicsObject RATHER THAN A QGraphicsItem, and that is a phase E decision
// taken here because it costs nothing here. Section 18.2 animates a card from
// the hand to the table with QPropertyAnimation on `pos`, which needs the QObject
// and the property that only QGraphicsObject brings; changing the base class of
// every card later is a change to every file that touches one. Nothing in B2
// animates, and nothing in B2 pays for this.
//
// IT OWNS NO ART AND BUILDS NO PATH. Every pixel comes from CardRenderer, which
// section 18.1 makes the only thing that knows a card is a file - so this holds
// a pointer to one rather than a pixmap, and paint() is a blit of whatever that
// hands back for the size and ratio it is being drawn at.
class CardItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // The renderer outlives every item: TableScene owns it by value and the
    // items are its children. A reference would say that better, but a
    // QGraphicsObject is moved and reparented by the scene and a reference
    // member would make it unassignable for no gain.
    explicit CardItem(const CardRenderer* renderer, QGraphicsItem* parent = nullptr);

    // Face up. The card is the whole of what it draws.
    void setFace(romanian_whist::Card card);

    // Face down - an opponent's hand, a Forehead round, a Hidden one. Not a
    // special case anywhere but here, which is section 18.1's argument for
    // CardRenderer::back() existing at all.
    void setBack();

    // In SCENE units - table units, which the view scales to the window - and it
    // is the only size this takes: the width follows
    // from the art's aspect through CardRenderer::sizeForHeight(), so a card
    // cannot be set to a shape the art does not have.
    void setCardHeight(qreal height);

    // A CARD ON THE FELT (the table redesign's section 2.7): it casts a shadow,
    // and the winning card of a partly played trick rises toward its own top,
    // ringed in gold-bright and glowing. THE CALLER READS PlayedCardView::isWinning
    // AND NEVER RECOMPUTES IT - that is the engine's own ranking, so the
    // highlight cannot disagree with the winner it eventually declares.
    //
    // `shadows` is the picture cache every card that casts a shadow shares, owned
    // with the renderer, which outlives the item.
    void setPlayed(bool winning, const Theme::TablePalette& palette, cardshadow::Cache* shadows);

    // A CARD IN YOUR HAND (section 2.8): it casts the hand's shadow, and raised -
    // the playable card under the cursor - it rises toward its own top, ringed in
    // gold-bright and glowing. THE RISE IS DRAWN, NOT A MOVE: the item stays where
    // the fan put it and turned as the fan turned it, so the card rises along its
    // own tilt, and what lies under the cursor is decided by HandItem: where each
    // card rests, and where the raised one has risen to.
    void setHeld(bool raised, const Theme::TablePalette& palette, cardshadow::Cache* shadows);

    // What setPlayed() or setHeld() last said: READ-ONLY and for tests. None for a
    // card set neither, which casts nothing.
    std::optional<cardshadow::Look> look() const { return cast; }
    bool isPlayed() const { return cast == cardshadow::Look::Played || cast == cardshadow::Look::Winning; }
    bool isWinning() const { return cast == cardshadow::Look::Winning; }
    bool isRaised() const { return cast == cardshadow::Look::Raised; }

    // Darkened and dulled (CardRenderer::State::Dimmed), baked into the cached
    // pixmap by CardRenderer rather than applied here, so this stays a blit.
    void setDimmed(bool dimmed);

    // The card itself, in table units, from the art's aspect: empty without a
    // renderer or a height. Not boundingRect(), which a played card's shadow and
    // rise reach past.
    QSizeF cardSize() const;

    // That size where paint() draws it, centred on the origin and before any rise
    // (cardshadow::cardRect()). Not boundingRect() either. What "on the card" means
    // to HandItem's hit test and to the tests.
    QRectF cardRect() const;

    // ORIGIN AT THE CENTRE, not the top left. A card is placed in front of its
    // owner's rack and turned to line up with it, and both of those are one line
    // when the origin is the middle of the card.
    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:

    const CardRenderer* renderer;

    // Disengaged is face down. A card and a bool would let the two disagree.
    std::optional<romanian_whist::Card> card;

    qreal cardHeight = 0.0;
    bool dimmed = false;

    // The shadow's picture, asked of the cache only when it is stale or the ratio
    // has moved, through the same PictureMemo the racks and panels keep theirs in,
    // rather than on every paint.
    const cardshadow::Picture& shadow(QSizeF size, qreal ratio) const;

    // What it casts: on the felt or in your hand, and how. The colours are the
    // shadows', the glows' and the ring's - a few of the table's, not all of
    // them - and the cache is where their picture comes from.
    void setCast(cardshadow::Look look, const Theme::TablePalette& palette, cardshadow::Cache* shadows);

    std::optional<cardshadow::Look> cast;
    cardshadow::Colours colours;
    cardshadow::Cache* shadows = nullptr;

    mutable PictureMemo shadowMemo;
};

#endif
