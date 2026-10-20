#ifndef HAND_ITEM_H
#define HAND_ITEM_H

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/CardShadow.h"
#include "paint/Theme.h"
#include "table/TableSpace.h"

#include <QGraphicsObject>
#include <QRectF>
#include <QSizeF>

#include <cstddef>
#include <optional>
#include <vector>

class CardItem;
class CardRenderer;

// WHERE YOUR CARDS GO, which is the part of the fan with a right answer - so a free
// function, as trickitem's and rackitem's are, checked with no scene.
//
// The table redesign's section 2.8, from table.js renderHand(). The cards step
// along the bottom of the rim, centred on the table: 84 apart, closing up so the fan
// is never wider than 720, which a full hand of eight is - each card then covers
// about half of the one before it. They drop away toward the ends on an arc, and
// each is turned 1.9 degrees for every step it is from the middle, about a hinge
// 1.3 card-heights below its own top: under your panel, so the fan opens from it.
namespace handitem
{

// SLOT `slot` OF A FAN OF `count` cards of `card` size, in table units: where the
// card's centre goes and how far it is turned. `slot` counts from the left and must
// be below `count`. Before the arc and the turn every card's top is
// tablespace::handTop, so the middle card of an odd hand stands exactly there.
tablespace::Placement cardPlacement(std::size_t count, std::size_t slot, QSizeF card);

} // namespace handitem

// Your hand, fanned along the bottom of the table.
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
    // owns it by value and the items are its children. `shadows` is the cards'
    // shadow cache, owned with it and shared with the played cards; null gives the
    // fan one of its own.
    explicit HandItem(const CardRenderer* renderer, cardshadow::Cache* shadows = nullptr,
                      QGraphicsItem* parent = nullptr);

    // BETWEEN TURNS. Drawn disabled whatever this says, because a snapshot's hand
    // is for looking at - see the class comment.
    //
    // IGNORED WHILE A PROMPT IS LIVE, and that is not a convenience: TableScene
    // re-sources the fan from its last snapshot on every theme change, and the
    // system's colour scheme can flip in the middle of your turn - as every
    // resize could until the table redesign's phase 2. See the definition for
    // what taking it there would cost.
    void setHand(const std::vector<HandEntry>& hand);

    // YOUR TURN. Legality that means something, or - in a blind round, where
    // `entries` is empty and `blind` is set - handIndex.size() face-down backs,
    // every one of them playable. The hand is NOT SENT AND THEN NOT DRAWN in
    // those rounds; it is not sent, and a count of positions is all there is to
    // draw from (section 17).
    void setPrompt(const PlayPrompt& prompt);

    // Nothing to show: between games, and after the fan has been answered.
    void clear();

    // The cards' height, in table units: tablespace::handCardHeight on the table.
    // Their width follows from the art. The fan's steps, arc and turn are section
    // 2.8's own numbers, so a card of another size is spaced the same.
    void setCardHeight(qreal height);

    void setTheme(const Theme& theme);

    // Whether a click does anything. The fan is drawn either way: a disabled hand
    // is still the hand you are looking at while a bot thinks.
    //
    // FALSE ALSO ENDS THE PROMPT'S CLAIM on the fan - an answer submitted, or a
    // game that stopped - so the next snapshot is drawn again. There is no
    // separate "the turn is over" call because there is no state where the hand
    // is unplayable and the prompt still owns what is drawn.
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

    // Every card where cardPlacement() puts it, and the box they cover. Then
    // showLift(), which is all a hover changes: which card is raised.
    void layOut();
    void showLift();

    // WHICH SLOT IS UNDER A POINT, and it is not "the first whose box contains
    // it". The cards overlap by about half and later ones are drawn ON TOP, so
    // the card a player is pointing at is the LAST one that contains the point.
    // First-match returns the card underneath, and every click in an overlap -
    // which is most of the fan - plays a neighbour.
    //
    // IN EACH CARD'S OWN FRAME, since every card is turned, and on the card as it
    // RESTS - plus, for the raised card, where it has risen to. That is the
    // mock-up's rule: the browser hovers the button, which stays where it is, and
    // the face, which rises. Tested on the risen card alone, the strip it rose
    // from would let the cursor go, the card would drop back under it, and it
    // would rise again on the next move: a card that flickers.
    std::optional<std::size_t> slotAt(QPointF position) const;

    const CardRenderer* renderer;

    // The cards' shadows, shared with the played cards unless the fan was given
    // none (see the constructor).
    cardshadow::Cache ownShadows;
    cardshadow::Cache* shadows;

    Theme theme = Theme::light();

    std::vector<Slot> fan;

    // What is currently being shown, so a rebuild knows what to build. Faces or
    // backs, never both: a blind round is all backs and every other round is all
    // faces.
    std::vector<HandEntry> entries;
    std::size_t backs = 0;

    qreal cardHeight = 0.0;
    bool playable = false;

    // Every card at rest and risen, in this item's coordinates: what the fan can be
    // hit on, and so what boundingRect() is. It does not depend on which card is
    // raised, so a hover moves no geometry.
    QRectF bounds;

    // WHICH OF THE TWO SOURCES is currently on screen. The class comment above is
    // about there being two; this is the one bit that says which, and it is here
    // because the two arrive from different places and can arrive out of turn - a
    // theme change re-sources the snapshot at any moment, including while a prompt
    // is waiting to be answered. The prompt takes the fan and keeps it until the
    // hand stops being playable.
    enum class Source
    {
        Snapshot,
        Prompt
    };

    Source source = Source::Snapshot;

    // The slot under the cursor, raised. Disengaged when the cursor is off the
    // fan, on a card that cannot be played, or the hand is not playable.
    std::optional<std::size_t> hovered;
};

#endif
