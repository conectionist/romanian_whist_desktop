#ifndef CARD_ITEM_H
#define CARD_ITEM_H

#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QGraphicsObject>

#include <optional>

class CardRenderer;

namespace cardshadow
{
class Cache;
}

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
    // `shadows` is the picture cache every played card shares, owned with the
    // renderer, which outlives the item. A card in your hand is never set
    // played, and casts nothing.
    void setPlayed(bool winning, const Theme::TablePalette& palette, cardshadow::Cache* shadows);

    // What setPlayed() last said. READ-ONLY and for tests.
    bool isPlayed() const { return played; }
    bool isWinning() const { return winning; }

    // 45% opacity plus a slight desaturation, baked into the cached pixmap by
    // CardRenderer rather than applied here, so this stays a blit.
    void setDimmed(bool dimmed);

    // The card itself, in table units, from the art's aspect: empty without a
    // renderer or a height. Not boundingRect(), which a played card's shadow and
    // rise reach past.
    QSizeF cardSize() const;

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

    // On the felt, and the trick's winner. The palette is the shadow's and the
    // ring's, and the cache is where their picture comes from.
    bool played = false;
    bool winning = false;
    Theme::TablePalette palette;
    cardshadow::Cache* shadows = nullptr;
};

#endif
