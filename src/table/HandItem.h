#ifndef HAND_ITEM_H
#define HAND_ITEM_H

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QGraphicsObject>

#include <cstddef>
#include <optional>
#include <vector>

class CardItem;
class CardRenderer;

// Your hand, fanned across the bottom of the table.
//
// TWO SOURCES, NEVER ONE, and section 4.0 is the whole of why. Between turns the
// fan comes from GameSnapshot::hand and is drawn DISABLED: a snapshot carries no
// legality - HandEntry::legal is always true on one - and it is EMPTY in a
// Forehead or Hidden round, so it could never be the thing a player clicks. On
// your turn it is redrawn from PlayPrompt, which is the only object in the
// program holding the cards, the legal set and the way back to the engine's own
// hand positions at once. A fan that only ever read the snapshot cannot be
// clicked and shows nothing for the last two rounds of every default game.
//
// A QGraphicsObject in the scene, not a widget overlay like BidStrip. Two
// reasons, and the first is the one that would be expensive to undo: section
// 18.2 animates a card from here to the table with QPropertyAnimation on
// CardItem::pos, and the cards are already QGraphicsObjects. The second is that
// the fan overlaps the table's own space and has to sort with it.
class HandItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // The renderer outlives every item, exactly as CardItem documents: TableScene
    // owns it by value and the items are its children.
    explicit HandItem(const CardRenderer* renderer, QGraphicsItem* parent = nullptr);

    // BETWEEN TURNS. Drawn disabled whatever this says, because a snapshot's hand
    // is for looking at - see the class comment.
    void setHand(const std::vector<HandEntry>& hand);

    // YOUR TURN. Legality that means something, or - in a blind round, where
    // `entries` is empty and `blind` is set - handIndex.size() face-down backs,
    // every one of them playable. The hand is NOT SENT AND THEN NOT DRAWN in
    // those rounds; it is not sent, and a count of positions is all there is to
    // draw from (section 17).
    void setPrompt(const PlayPrompt& prompt);

    // Nothing to show: between games, and after the fan has been answered.
    void clear();

    // HOW FAR A HOVERED CARD RISES, as a fraction of the card's height.
    //
    // PUBLIC AND STATIC because TableScene has to reserve room for it before any
    // card exists, and a second copy of the number there is a second copy that
    // drifts: the strip would stop being tall enough for the lift the day either
    // moved. Same argument TrickItem::cardFootprint() makes for the tilt.
    static qreal liftFraction();

    // The unit the rest of the table is laid out in. The fan's own metrics -
    // overlap, lift, the box it occupies - all derive from it.
    void setCardHeight(qreal height);

    void setTheme(const Theme& theme);

    // Whether a click does anything. The fan is drawn either way: a disabled hand
    // is still the hand you are looking at while a bot thinks.
    void setPlayable(bool playable);

    // How many cards are fanned, which is what a caller needs to know before it
    // can talk about fan - the digit keys, most of all.
    std::size_t slotCount() const;

    // Whether a click on this slot would do anything: in range, playable, and
    // legal. Public because the keyboard asks the same question the mouse does
    // and the two must not answer it differently.
    bool isPlayable(std::size_t slot) const;

    // Plays a slot as though it had been clicked. The digit keys go through here
    // rather than emitting the signal themselves, so both paths run one rule.
    void activate(std::size_t slot);

    QRectF boundingRect() const override;

    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}

signals:
    // THE DISPLAY SLOT - never a card, and never a position in the engine's hand.
    // The translation through PlayPrompt::handIndex belongs to whoever holds the
    // prompt, which is MainWindow, and it is the single mapping that decides
    // whether the card played is the card clicked. Emitting a hand position from
    // here would put that mapping in two places.
    void cardClicked(std::size_t slot);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    // One entry per fanned card. `legal` is meaningless on a snapshot hand and
    // decisive on a prompt, which is the distinction the class comment is about.
    //
    // THE VECTOR BELOW IS NAMED `fan` AND NOT `slots`, which is not a style
    // choice: `slots` is a Qt macro that expands to nothing, so the member
    // declares away to `std::vector<Slot> ;` and the compiler reports
    // "declaration does not declare anything" against a line that looks correct.
    struct Slot
    {
        CardItem* item = nullptr;
        bool legal = true;
    };

    void rebuild();
    void layOut();

    // WHICH SLOT IS UNDER A POINT, and it is not "the first whose box contains
    // it". The cards overlap by 40% of their width and later ones are drawn ON
    // TOP, so the card a player is pointing at is the LAST one that contains the
    // point. First-match returns the card underneath, and every click in an
    // overlap - which is most of the fan - plays a neighbour.
    std::optional<std::size_t> slotAt(QPointF position) const;

    const CardRenderer* renderer;

    Theme theme = Theme::light();

    std::vector<Slot> fan;

    // What is currently being shown, so a rebuild knows what to build. Faces or
    // backs, never both: a blind round is all backs and every other round is all
    // faces.
    std::vector<HandEntry> entries;
    std::size_t backs = 0;

    qreal cardHeight = 0.0;
    bool playable = false;

    // The slot under the cursor, lifted. Disengaged when the cursor is off the
    // fan or the hand is not playable.
    std::optional<std::size_t> hovered;
};

#endif
