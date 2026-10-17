#ifndef CARD_ITEM_H
#define CARD_ITEM_H

#include <romanian_whist/Card.h>

#include <QColor>
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

    // The winning card of a partly played trick: a raised shadow and a thin
    // accent hairline (section 18). THE CALLER READS PlayedCardView::isWinning
    // AND NEVER RECOMPUTES IT - that is the engine's own ranking, so the
    // highlight cannot disagree with the winner it eventually declares.
    void setWinning(bool winning, QColor accent);

    // 45% opacity plus a slight desaturation, baked into the cached pixmap by
    // CardRenderer rather than applied here, so this stays a blit.
    void setDimmed(bool dimmed);

    // ORIGIN AT THE CENTRE, not the top left. A card is placed at a seat's
    // centre and rotated toward the middle of the table, and both of those are
    // one line when the origin is the middle of the card.
    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QSizeF cardSize() const;

    const CardRenderer* renderer;

    // Disengaged is face down. A card and a bool would let the two disagree.
    std::optional<romanian_whist::Card> card;

    qreal cardHeight = 0.0;
    bool winning = false;
    bool dimmed = false;
    QColor accent;
};

#endif
