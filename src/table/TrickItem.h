#ifndef TRICK_ITEM_H
#define TRICK_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/CardShadow.h"
#include "paint/Theme.h"
#include "table/Motion.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"

#include <romanian_whist/Card.h>

#include <QGraphicsObject>
#include <QPointF>

#include <functional>
#include <optional>
#include <vector>

class CardItem;
class CardRenderer;
class QVariantAnimation;

// WHERE THE TRICK GOES, which is the part of it with a right answer - so free
// functions, the split rackitem and seatlayout follow, checked with no scene.
//
// The table redesign's section 2.7, from table.js trickPos() and renderTrick():
// each opponent's card lands squarely in front of their rack, turned to line up
// with it, and yours just above your hand.
namespace trickitem
{

// WHERE `seat`'S PLAYED CARD LIES.
//
// An opponent's - and a spectated seat 0's, which is drawn as one - is on the line
// from the table's centre at its rack's angle (seatlayout::rackAngle()), its outer
// end tablespace::playedInset inside the felt's edge, and turned as the rack is
// (rackitem::frameRotation()): so the side seats' cards lie sideways and the top
// seat's is upside down, each reading the right way up to its owner. A quarter turn
// exactly when it is one.
//
// Yours is centred above your hand, its bottom edge playedInset above the hand's
// top, and not turned.
//
// From the seat's place, worked out once (seatlayout::place()); the card's size
// is the table's, by `seatCount`.
tablespace::Placement cardPlacement(unsigned int seatCount, const seatlayout::Place& place);

// The same for a seat by number: none for a seat with no panel - a player count
// section 2.5 has no row for, or a seat that is not at the table.
std::optional<tablespace::Placement> cardPlacement(unsigned int seatCount, int humanSeat, unsigned int seat);

// WHERE THE LEAD RHOMBUS GOES FOR `seat`: an opponent's on the felt's gold circle
// at its rack's angle, between the rack and the card, turned so its long axis
// points at the rack; yours in the gap between your card and your hand, where the
// circle is under your cards. None where cardPlacement() has none.
tablespace::Placement leadMarkPlacement(const seatlayout::Place& place);

std::optional<tablespace::Placement> leadMarkPlacement(unsigned int seatCount, int humanSeat,
                                                       unsigned int seat);

// WHICH SEAT THE RHOMBUS MARKS, if any: only while the engine's phase is Playing,
// and then whoever played the trick's first card - or, before it is down,
// snapshot.leadSeat, the engine's leader for the trick about to start.
//
// THE PHASE, NOT THE BIDS AND THE SEAT ON TURN. After the last bid is recorded the
// bidder is still on turn, and after the round's last trick the finished trick is
// still on the table; neither is a trick being led, and only the phase says so.
std::optional<unsigned int> leadSeat(const GameSnapshot& snapshot);

} // namespace trickitem

// The trick, growing in the middle of the table.
//
// Each card lies in front of its owner's rack, so who played what is read off the
// table rather than off a list of names - which is section 14's whole argument for
// a GUI over the terminal's "Ana played the king of hearts".
//
// SEATS COME OFF THE TRICK, NOT OFF ARITHMETIC. PlayedCardView carries
// seatIndex for exactly this reason: getTrickLeaderSeat() has already moved to
// the WINNER by the time the trick-end frame is drawn, so a table rebuilt as
// (leader + i) % playerCount draws the last trick of every round under the wrong
// names. IGameObserver.h says so in its onTrickWon comment, the terminal's
// rebuildTable() carries the same note, and Snapshot.cpp's own rebuildTable()
// has it a third time. This walks snapshot.table and reads the seat off each
// entry.
//
// IT DRAWS NOTHING ITSELF: the cards are its children, and the lead rhombus is
// LeadMarkItem, a rung above them in tablespace::z.
//
// A CARD FLIES TO THE FELT (phase 12), when the pace says so and a frame adds one
// to the trick the last frame showed: from your hand for yours, from the owner's
// panel for a bot's, as table.js's animateIn() does. THE SCENE'S OWN, and nothing
// the game waits for: it is worked out from two frames side by side, and a frame
// that does not agree with it - a trick cleared, a new round, a card that is not the
// one flying - ends it where it was going. The target is always where
// trickitem::cardPlacement() puts the card, so a flight and a card at rest end in the
// same place by construction.
//
// AND A FINISHED TRICK SWEEPS TO ITS WINNER as the felt clears (table.js sweep()):
// the frame after the full trick has an empty table, and the cards it no longer
// shows shrink and fade toward the winner's panel. Not at a round's end - the last
// trick stays under the round sheet, as it always has - and not past the next card
// put down, which ends the sweep where it was going.
class TrickItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // The renderer outlives every item, as CardItem documents. `shadows` is the
    // played cards' shadow cache, owned with it; null gives the item one of its own.
    explicit TrickItem(const CardRenderer* renderer, cardshadow::Cache* shadows = nullptr,
                       QGraphicsItem* parent = nullptr);

    // WHERE A CARD OF YOURS STOOD IN THE FAN, asked at the moment a card of yours is
    // new on the felt, before the fan is redrawn without it: HandItem::
    // restingPlacement(). Empty for a table with no fan, or a card it does not hold,
    // and the card then flies from your panel as a bot's does.
    using HandPlace = std::function<std::optional<tablespace::Placement>(const romanian_whist::Card&)>;

    // Every card on the table, each where trickitem::cardPlacement() puts it.
    // `places` is every seat's, seatlayout::places() for the snapshot's table, as
    // TableScene has already worked them out for the racks.
    //
    // A card new since the last frame - the same round, the last table a prefix of
    // this one - flies to its place when the pace animates (setPace()); every other
    // card is placed at once. `fromHand` is asked only for a card new in this frame.
    void setTrick(const GameSnapshot& snapshot, const seatlayout::Places& places, const Theme& theme,
                  const HandPlace& fromHand = {});

    // A NEW GAME: the felt goes bare, every flight and sweep ends, and the next frame
    // is a first frame, with nothing to fly from or sweep. Without it, a new game's
    // first frame in the same round, with as many seats, read as the frame that
    // cleared the last game's final trick and swept it (PR #38's review, finding #1).
    void clear();

    // HOW MUCH THE TRICK MOVES. Off by default, so an item on its own places every
    // card at once; TableScene hands over Settings' choice. Going to Off ends every
    // flight where it was going.
    void setPace(motion::Pace pace);
    motion::Pace pace() const { return currentPace; }

    // THE ANIMATION BEHIND card `index`'s flight, null for a card at rest: for tests,
    // which drive it with pause() and setCurrentTime() rather than sleep. Never null
    // while a flight is under way.
    QVariantAnimation* flight(std::size_t index) const;
    bool isFlying() const;

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    // One card's flight: what it starts from, what it ends at, and the clock between.
    // `to` is rewritten by every frame the card is still flying in, so a flight is
    // always on its way to where the card now belongs.
    struct Tween
    {
        tablespace::Placement from;
        tablespace::Placement to;
        qreal fromScale = 1.0;
        qreal fromOpacity = 1.0;
        qreal toScale = 1.0;
        qreal toOpacity = 1.0;

        // Whether the card is in the air, whether it left your hand (which is what
        // takes the trick over the fan: tablespace::z::flyingCard), and whether it is
        // leaving the felt in a sweep, to be hidden when it gets there.
        bool active = false;
        bool fromHand = false;
        bool leaving = false;

        QVariantAnimation* clock = nullptr;
    };

    // Grown on demand and then reused for the life of the scene, never cleared
    // and rebuilt per frame: six items at most, ~1300 frames a game. A CardItem that
    // outlives the frame it appeared in is what a flight needs - and so does each
    // keep its animation, which is reused rather than made again for every card.
    CardItem* cardAt(std::size_t index);

    // What the last frame showed, to tell which cards a new one adds.
    struct Shown
    {
        unsigned int seat = 0;
        romanian_whist::Card card{};
        bool winning = false;
    };

    // WHETHER THE FLIGHTS UNDER WAY AGREE WITH THIS FRAME, and where its new cards
    // begin. A frame agrees when the same round's last table is a prefix of this
    // one - equal, or with cards added. It does not when a trick was cleared, a new
    // round came, or a card is not the one that was there.
    struct Arrivals
    {
        bool agrees = false;
        std::size_t first = 0;
    };

    Arrivals arrivalsFor(const GameSnapshot& snapshot) const;

    // Starts card `index` on its flight to `place`, from where a card of its owner's
    // comes: `fromHand` for yours, the owner's panel for the rest.
    void launch(std::size_t index, const seatlayout::Place& seat, const tablespace::Placement& place,
                qreal cardHeight, const romanian_whist::Card& card, const HandPlace& fromHand);

    // Whether this frame clears a finished trick the last frame showed, in the same
    // round, so it sweeps to its winner; and if so, the winner's seat.
    std::optional<unsigned int> sweepWinner(const GameSnapshot& snapshot) const;

    // Sends every card the last frame showed toward `winner`'s panel.
    void sweep(const seatlayout::Place& winner);

    // Ends every sweep under way: the cards it was carrying off are hidden.
    void finishLeaving();

    // One tick of card `index`'s flight, `progress` from 0 to 1 after the curve.
    void step(std::size_t index, qreal progress);

    // Ends card `index`'s flight where it was going. Safe for a card at rest.
    void finish(std::size_t index);
    void finishAll();

    // The trick's rung: over your hand while a card of yours is leaving it.
    void updateStacking();

    const CardRenderer* renderer;

    cardshadow::Cache ownShadows;
    cardshadow::Cache* shadows;

    std::vector<CardItem*> cards;

    // Parallel to `cards`.
    std::vector<Tween> tweens;

    motion::Pace currentPace = motion::Pace::Off;

    // The last frame shown, and the round it was in. `seen` is false until the first,
    // which has nothing to fly from.
    bool seen = false;
    unsigned int shownRound = 0;
    std::vector<Shown> shown;
};

#endif
