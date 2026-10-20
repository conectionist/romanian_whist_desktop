#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TableFixtures.h"

#include "table/TableScene.h"
#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/LeadMarkItem.h"
#include "table/RackItem.h"
#include "table/SeatItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"
#include "table/TrickItem.h"
#include "engine/Prompts.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QCoreApplication>
#include <QGraphicsItem>
#include <QList>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

using Catch::Approx;
using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// ---- table space, added in the table redesign's phase 2 ----
//
// The scene is a fixed set of coordinates the view scales, so where everything
// goes is a fact about the scene alone - asserted here in table units, against
// section 2's numbers rather than against the scene's own arithmetic.

namespace
{
GameSnapshot seatedTable(unsigned int seats, int humanSeat)
{
    GameSnapshot snapshot;

    snapshot.humanSeat = humanSeat;
    snapshot.roundNumber = 1;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.seats.resize(seats);

    for(unsigned int i = 0 ; i < seats ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].isHuman = static_cast<int>(i) == humanSeat;
    }

    return snapshot;
}

// A seated table with three cards in the human's hand, so the fan has something
// to measure.
GameSnapshot holdingThree(unsigned int seats, int humanSeat)
{
    GameSnapshot snapshot = seatedTable(seats, humanSeat);

    snapshot.hand = { HandEntry{ Card(Rank::Two, Suit::Clubs) },
                      HandEntry{ Card(Rank::Ten, Suit::Hearts) },
                      HandEntry{ Card(Rank::Ace, Suit::Spades) } };

    return snapshot;
}

template<typename Item>
std::vector<Item*> itemsOf(const QGraphicsScene& scene)
{
    std::vector<Item*> found;

    for(QGraphicsItem* item : scene.items())
    {
        if(Item* match = dynamic_cast<Item*>(item))
            found.push_back(match);
    }

    return found;
}

// The plaque whose centre is the human's spot, section 2.5's (768, 1105). Found by
// where it is rather than by seat index, because that is the claim: the seated
// player's plaque is at the bottom.
SeatItem* plaqueAtBottom(const QGraphicsScene& scene)
{
    for(SeatItem* seat : itemsOf<SeatItem>(scene))
    {
        if(seat->isVisible() && seat->sceneBoundingRect().contains(QPointF(768.0, 1105.0)))
            return seat;
    }

    return nullptr;
}

// The played cards, which are TrickItem's children.
std::vector<CardItem*> playedCards(const QGraphicsScene& scene)
{
    std::vector<CardItem*> played;

    for(CardItem* card : itemsOf<CardItem>(scene))
    {
        if(card->isVisible() && dynamic_cast<TrickItem*>(card->parentItem()))
            played.push_back(card);
    }

    return played;
}
}

TEST_CASE("The scene is the fit box, whatever the window", "[table]")
{
    // A resize changes the view's transform and nothing here, so there is no
    // size for the scene to be told - it is born the size it will always be.
    TableScene scene;

    REQUIRE(scene.sceneRect() == tablespace::fitBox);

    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    REQUIRE(scene.sceneRect() == tablespace::fitBox);
}

TEST_CASE("A seated table holds the hand where section 2.8 puts it", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    REQUIRE(hand->isVisible());
    REQUIRE(hand->slotCount() == 3);

    // THE FAN LAYS ITSELF OUT IN TABLE SPACE, so the item is at the scene's origin
    // and each card where handitem::cardPlacement() puts it: the mock-up's 225 tall,
    // centred on the table, the middle one of three square to it with its top at
    // y = 850.
    REQUIRE(hand->pos() == QPointF(0.0, 0.0));

    std::vector<CardItem*> cards;

    for(CardItem* card : itemsOf<CardItem>(scene))
    {
        if(card->parentItem() == hand)
            cards.push_back(card);
    }

    REQUIRE(cards.size() == 3);

    std::sort(cards.begin(), cards.end(),
              [](const CardItem* a, const CardItem* b) { return a->zValue() < b->zValue(); });

    for(std::size_t i = 0 ; i < cards.size() ; i++)
    {
        INFO("slot " << i);

        const QSizeF size = cards[i]->cardSize();
        const tablespace::Placement place = handitem::cardPlacement(3, i, size);

        REQUIRE(size.height() == Catch::Approx(tablespace::handCardHeight));
        REQUIRE(cards[i]->scenePos().x() == Catch::Approx(place.centre.x()));
        REQUIRE(cards[i]->scenePos().y() == Catch::Approx(place.centre.y()));
        REQUIRE(cards[i]->rotation() == Catch::Approx(place.rotation).margin(1e-12));
    }

    const QSizeF middle = cards[1]->cardSize();
    const QPointF top = cards[1]->mapToScene(QPointF(0.0, -middle.height() / 2.0));

    REQUIRE(top.x() == Catch::Approx(tablespace::centre.x()));
    REQUIRE(top.y() == Catch::Approx(tablespace::handTop));
}

TEST_CASE("A spectated table draws no hand", "[table]")
{
    // SECTION 14'S RULE: a spectated game "has no human... and the bottom hand
    // area is not drawn at all". Seat 0 takes the human's spot instead.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, -1)));

    REQUIRE_FALSE(scene.handItem()->isVisible());
    REQUIRE(plaqueAtBottom(scene) != nullptr);
}

// ---- the racks, added in the redesign's phase 5 ----

namespace
{
std::vector<RackItem*> visibleRacks(const QGraphicsScene& scene)
{
    std::vector<RackItem*> racks;

    for(RackItem* rack : itemsOf<RackItem>(scene))
    {
        if(rack->isVisible())
            racks.push_back(rack);
    }

    return racks;
}
}

TEST_CASE("Every opponent has a rack on the rim and you have none", "[table]")
{
    // Section 2.6: your hand is the fan at the bottom, so a rack of your own would
    // show your cards twice - as backs.
    for(unsigned int count = 2 ; count <= 6 ; count++)
    {
        TableScene scene;

        GameSnapshot snapshot = holdingThree(count, 1);
        for(SeatView& seat : snapshot.seats)
            seat.cardsLeft = 3;

        scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

        const std::vector<RackItem*> racks = visibleRacks(scene);

        INFO(count << " seats");
        REQUIRE(racks.size() == count - 1);

        for(RackItem* rack : racks)
        {
            REQUIRE(QLineF(tablespace::centre, rack->pos()).length() == Approx(tablespace::rackRadius));
            REQUIRE(rack->shown().count == 3);

            // None at the bottom, where your hand is.
            REQUIRE(rack->pos().y() < tablespace::handTop);
        }
    }
}

TEST_CASE("A spectated table stands seat 0's rack at the bottom", "[table]")
{
    // Section 2.5: with nobody seated, seat 0 takes the bottom spot and is drawn as an
    // opponent - a rack on the rim at 90 degrees, and no hand.
    TableScene scene;

    GameSnapshot snapshot = seatedTable(4, -1);
    for(SeatView& seat : snapshot.seats)
        seat.cardsLeft = 2;

    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    REQUIRE(visibleRacks(scene).size() == 4);

    const QPointF bottom = seatlayout::rackOrigin(std::numbers::pi / 2.0);
    bool found = false;

    for(RackItem* rack : visibleRacks(scene))
        found = found || QLineF(rack->pos(), bottom).length() < 1e-9;

    REQUIRE(found);
}

TEST_CASE("Your own seat shows no face in a Forehead round", "[table]")
{
    // THE CASE THAT WOULD LEAK YOUR OWN CARD TO YOU, re-homed from SeatItemTests when
    // the redesign's phase 5 moved the Forehead card into the racks. A Forehead round
    // as the engine publishes it: every other seat's single card on its SeatView,
    // yours on none - canSeeHand(human, human) is false - and no hand of your own.
    // Every other rack shows its owner's card face up; yours does not exist, and no
    // panel draws a card at all.
    GameSnapshot snapshot = seatedTable(4, 1);
    snapshot.roundType = romanian_whist::RoundType::Forehead;

    const Card cards[] = { Card(Rank::Ace, Suit::Hearts), Card(Rank::Two, Suit::Clubs),
                           Card(Rank::King, Suit::Spades), Card(Rank::Ten, Suit::Diamonds) };

    for(unsigned int i = 0 ; i < 4 ; i++)
    {
        snapshot.seats[i].cardsLeft = 1;

        if(static_cast<int>(i) != snapshot.humanSeat)
            snapshot.seats[i].hand.push_back(HandEntry{ cards[i] });
    }

    TableScene scene;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    const std::vector<RackItem*> racks = visibleRacks(scene);
    REQUIRE(racks.size() == 3);

    std::vector<Card> faces;

    for(RackItem* rack : racks)
    {
        REQUIRE(rack->shown().faces.size() == 1);
        faces.push_back(rack->shown().faces.front());
    }

    for(unsigned int i = 0 ; i < 4 ; i++)
    {
        const bool shown = std::find(faces.begin(), faces.end(), cards[i]) != faces.end();

        INFO("seat " << i);
        REQUIRE(shown == (static_cast<int>(i) != snapshot.humanSeat));
    }

    for(SeatItem* seat : itemsOf<SeatItem>(scene))
        REQUIRE(seat->childItems().isEmpty());
}

TEST_CASE("Your own plaque is drawn over your hand", "[table]")
{
    // SECTION 2.8: "the human's panel covers the bottom of the hand; that is
    // intended". The two overlap in table space, so which is on top is a real
    // question - and the plaque carries your name and your pill, so it has to be
    // the answer.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);

    // The premise, asserted rather than assumed: they do overlap.
    REQUIRE(mine->sceneBoundingRect().intersects(scene.handItem()->sceneBoundingRect()));

    REQUIRE(mine->zValue() > scene.handItem()->zValue());
}

// ---- the stacking order, set out in the redesign's phase 3 ----
//
// SECTION 3'S LADDER, read off the items as the scene built them: the table under
// the played cards under your hand under your plaque under everyone else's. Each
// against the next, so a rung put in the wrong place fails here by name rather
// than as a card drawn under the felt.

TEST_CASE("The scene stacks the table then the racks then the cards then the rhombus then your hand then the plaques", "[table]")
{
    GameSnapshot snapshot = holdingThree(4, 1);
    snapshot.betsPlaced = 4;
    snapshot.phase = romanian_whist::GamePhase::Playing;
    snapshot.table = { PlayedCardView{ 2, Card(Rank::King, Suit::Hearts) } };

    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    const std::vector<TableTopItem*> tables = itemsOf<TableTopItem>(scene);
    const std::vector<TrickItem*> tricks = itemsOf<TrickItem>(scene);

    REQUIRE(tables.size() == 1);
    REQUIRE(tricks.size() == 1);

    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);

    const std::vector<RackItem*> racks = itemsOf<RackItem>(scene);
    REQUIRE_FALSE(racks.empty());

    // THE RACKS ON THE RIM, under every card and every panel: table.js builds them
    // first "so each plaque sits over the base of its own fan".
    for(RackItem* rack : racks)
    {
        REQUIRE(tables.front()->zValue() < rack->zValue());
        REQUIRE(rack->zValue() < tricks.front()->zValue());
    }

    REQUIRE(tables.front()->zValue() < tricks.front()->zValue());

    // THE RHOMBUS OVER THE CARDS, as table.js adds it after them - though where it
    // sits, on the gold circle, no card reaches.
    const std::vector<LeadMarkItem*> marks = itemsOf<LeadMarkItem>(scene);

    REQUIRE(marks.size() == 1);
    REQUIRE(marks.front()->isVisible());
    REQUIRE(tricks.front()->zValue() < marks.front()->zValue());
    REQUIRE(marks.front()->zValue() < scene.handItem()->zValue());

    REQUIRE(scene.handItem()->zValue() < mine->zValue());

    for(SeatItem* seat : itemsOf<SeatItem>(scene))
    {
        if(seat != mine && seat->isVisible())
            REQUIRE(mine->zValue() < seat->zValue());
    }

    // And the table is the lowest thing in the scene, whatever is added to it.
    for(QGraphicsItem* item : scene.items())
    {
        if(item != tables.front() && !item->parentItem())
            REQUIRE(tables.front()->zValue() < item->zValue());
    }
}

TEST_CASE("The table is there before the first snapshot", "[table]")
{
    // An empty table is still a table: the room and the felt are the first thing
    // the page shows, before a game has dealt anything onto it.
    TableScene scene;

    const std::vector<TableTopItem*> tables = itemsOf<TableTopItem>(scene);

    REQUIRE(tables.size() == 1);
    REQUIRE(tables.front()->isVisible());
    REQUIRE(tables.front()->sceneBoundingRect().center() == tablespace::centre);
}

// THE PLAQUE THAT IS YOURS CHANGES WITH THE GAME, and so does its rung. The items
// are reused from one game to the next, so a z set once, when a plaque was made,
// would leave last game's human above or below the wrong hand.
TEST_CASE("Your plaque's place in the stack follows who you are this game", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 3)));

    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);
    REQUIRE(mine->zValue() == tablespace::z::humanPanel);

    for(SeatItem* seat : itemsOf<SeatItem>(scene))
    {
        if(seat != mine)
            REQUIRE(seat->zValue() == tablespace::z::panels);
    }
}

// ---- your plaque over your hand, and the mouse ----
//
// DRAWN ON TOP IS NOT THE SAME AS ON TOP FOR THE MOUSE. Qt hands a press to the
// items under it top first, but an item that does not take the press passes it
// down - and a plain QGraphicsItem does not take one. So the plaque covering the
// bottom of your fan let a click on your own name through to the card under it,
// and played that card. Hover went the same way: the hand is the only item that
// asks for hover, so it lifted a card the plaque was hiding.

namespace
{

// Your turn, with the three cards holdingThree() deals, all of them legal.
PlayPrompt playingThree()
{
    PlayPrompt prompt;

    prompt.promptId = 9;
    prompt.entries = holdingThree(4, 1).hand;
    prompt.handIndex = { 0, 1, 2 };

    return prompt;
}

// WHERE YOUR PLAQUE AND YOUR FAN OVERLAP. Your panel's top is at y = 1046 and the
// middle card's bottom at 1075 (section 2.8: 850 plus 225).
//
// AND ON TWO CARDS AT ONCE, x = 808 being where the third card overlaps the
// second. A press that got through the plaque would play one of them, whichever
// the hit test picks, and the case would see it.
const QPointF underYourPlaque(808.0, 1070.0);

// The middle card, well clear of the plaque.
const QPointF onTheCardAlone(768.0, 950.0);

// THROUGH THE SCENE, not to an item, because which item gets the event is the
// question. No view, so the scene finds the items under the point itself.
void pressAt(QGraphicsScene& scene, QPointF where)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);

    press.setScenePos(where);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &press);

    // And let go, so whatever took the press is not still holding the mouse when
    // the next one arrives.
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);

    release.setScenePos(where);
    release.setButton(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &release);
}

void hoverAt(QGraphicsScene& scene, QPointF where)
{
    QGraphicsSceneHoverEvent hover(QEvent::GraphicsSceneHoverMove);

    hover.setScenePos(where);

    QCoreApplication::sendEvent(&scene, &hover);
}

// A lifted card is a raised one: the rise is drawn by the card, which stays where
// the fan put it.
bool aCardIsLifted(const HandItem& hand)
{
    for(QGraphicsItem* child : hand.childItems())
    {
        const CardItem* card = dynamic_cast<const CardItem*>(child);

        if(card && card->isRaised())
            return true;
    }

    return false;
}

// Whether a point is on one of your cards as it rests, in the card's own frame:
// every card is turned, and its bounding rect reaches past it for the shadow.
bool onACard(const HandItem& hand, QPointF where)
{
    for(QGraphicsItem* child : hand.childItems())
    {
        const CardItem* card = dynamic_cast<const CardItem*>(child);

        if(!card)
            continue;

        if(card->cardRect().contains(card->mapFromScene(where)))
            return true;
    }

    return false;
}

// The premise both cases below rest on: the point really is on your plaque as
// drawn, and on one of your cards.
void requireOverlapAt(const TableScene& scene, QPointF where)
{
    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);
    REQUIRE(mine->contains(mine->mapFromScene(where)));

    REQUIRE(onACard(*scene.handItem(), where));
}

}

TEST_CASE("A click on your plaque does not play the card beneath it", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    hand->setPrompt(playingThree());
    hand->setPlayable(true);

    int clicks = 0;
    QObject::connect(hand, &HandItem::cardClicked, [&clicks](std::size_t) { clicks++; });

    requireOverlapAt(scene, underYourPlaque);

    pressAt(scene, underYourPlaque);

    REQUIRE(clicks == 0);

    // The control: the same card, clicked where nothing covers it, is played -
    // so the zero above is the plaque's doing and not a click that went nowhere.
    pressAt(scene, onTheCardAlone);

    REQUIRE(clicks == 1);
}

TEST_CASE("Hovering your plaque lifts no card", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    hand->setPrompt(playingThree());
    hand->setPlayable(true);

    requireOverlapAt(scene, underYourPlaque);

    // From nowhere straight onto the plaque.
    hoverAt(scene, underYourPlaque);

    REQUIRE_FALSE(aCardIsLifted(*hand));

    // The control: the card lifts where nothing covers it.
    hoverAt(scene, onTheCardAlone);

    REQUIRE(aCardIsLifted(*hand));
}

TEST_CASE("A card under a still cursor lifts when your turn comes", "[table]")
{
    // THROUGH THE SCENE'S OWN HOVER, as the app gets it: the cursor comes to rest on
    // a card between turns, when nothing lifts, and has not moved when the prompt
    // arrives. The scene sends nothing then, so the fan has to remember where the
    // cursor is.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    hoverAt(scene, onTheCardAlone);

    REQUIRE_FALSE(aCardIsLifted(*hand));

    // Your turn, as MainWindow deals it.
    hand->setPrompt(playingThree());
    hand->setPlayable(true);

    REQUIRE(aCardIsLifted(*hand));
}

TEST_CASE("Played cards are section 2.7's size, yours above your hand", "[table]")
{
    // Four seats with you in seat 1, and two cards down: yours and the next
    // seat's. Yours sits 60 above the top of your hand, unturned; the other in
    // front of its owner's rack, turned to line up with it, its outer end 60
    // inside the felt's edge.
    GameSnapshot snapshot = seatedTable(4, 1);

    snapshot.table = { PlayedCardView{ 1, Card(Rank::Ten, Suit::Hearts) },
                       PlayedCardView{ 2, Card(Rank::King, Suit::Hearts) } };

    TableScene scene;

    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    const std::vector<CardItem*> played = playedCards(scene);
    REQUIRE(played.size() == 2);

    const qreal height = tablespace::playedCardHeight(4);
    const QPointF yours(tablespace::centre.x(), tablespace::handTop - 60.0 - height / 2.0);
    const qreal radius = tablespace::feltRadius - 60.0 - height / 2.0;

    // Seat 2 is the first clockwise of you, on the left, where its rack is.
    const qreal rack = seatlayout::place(4, 1, 2)->angle.value();
    const QPointF theirs = tablespace::centre + radius * QPointF(std::cos(rack), std::sin(rack));

    bool sawYours = false;
    bool sawTheirs = false;

    for(CardItem* card : played)
    {
        // The art's own rounding of 205.2: sizeForHeight() hands back whole pixels.
        REQUIRE(card->cardSize().height() == Approx(std::round(height)));

        const QPointF at = card->scenePos();

        if(QLineF(at, yours).length() < 0.5)
        {
            sawYours = true;
            REQUIRE(card->rotation() == 0.0);
        }
        else if(QLineF(at, theirs).length() < 0.5)
        {
            sawTheirs = true;
            REQUIRE(card->rotation() == 90.0);
        }
    }

    REQUIRE(sawYours);
    REQUIRE(sawTheirs);

    // And the smaller card from five seats on, the same scene redrawn.
    snapshot.seats.resize(5);
    snapshot.seats[4].seatIndex = 4;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    // Still both cards, or the loop below checks nothing.
    REQUIRE(playedCards(scene).size() == 2);

    for(CardItem* card : playedCards(scene))
    {
        REQUIRE(card->cardSize().height()
                == Approx(std::round(tablespace::playedCardHeight(5))));
    }
}

TEST_CASE("The rhombus is on the felt only while a trick is played", "[table]")
{
    GameSnapshot snapshot = holdingThree(4, 1);

    TableScene scene;
    scene.setTheme(Theme::dark());

    const auto mark = [&]
    {
        const std::vector<LeadMarkItem*> marks = itemsOf<LeadMarkItem>(scene);
        REQUIRE(marks.size() == 1);
        return marks.front();
    };

    // Bidding: the seat being asked is bidding, not leading.
    snapshot.phase = romanian_whist::GamePhase::Betting;
    snapshot.betsPlaced = 2;
    snapshot.activeSeat = 3;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    REQUIRE_FALSE(mark()->isVisible());

    // The last bid's own frame: every bid in, the last bidder still on turn - but
    // still bidding, and not the leader.
    snapshot.betsPlaced = 4;
    snapshot.activeSeat = 2;
    snapshot.leadSeat = 3;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    REQUIRE_FALSE(mark()->isVisible());

    // Play begun, nothing played and nobody asked yet: on the seat that will lead,
    // in line with its rack on the gold circle.
    snapshot.phase = romanian_whist::GamePhase::Playing;
    snapshot.activeSeat = -1;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    REQUIRE(mark()->isVisible());
    REQUIRE(mark()->pos() == trickitem::leadMarkPlacement(4, 1, 3)->centre);

    // Your lead: between your card and your hand.
    snapshot.activeSeat = 1;
    snapshot.table = { PlayedCardView{ 1, Card(Rank::Ten, Suit::Hearts) } };
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    REQUIRE(mark()->isVisible());
    REQUIRE(mark()->pos() == QPointF(768.0, 820.0));

    // The round scored, its last trick still on the table: nothing marked.
    snapshot.phase = romanian_whist::GamePhase::RoundScored;
    snapshot.activeSeat = -1;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    REQUIRE_FALSE(mark()->isVisible());
}

// ---- the floor the bid strip sits on, added in C2 ----
//
// SECTION 15.1's strip "rises over the bottom of the table", and the bottom of
// the table is where your hand and your own plaque are. So "the bottom" and
// "where the strip goes" are different places, and getting that wrong is not
// subtle: C2's first run drew the strip straight through the player's own name.
//
// Geometry with a right answer, which is what this file is for.

TEST_CASE("The strip's floor clears your hand and your own plaque", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    const std::optional<qreal> floor = scene.overlayBottom();

    // THERE IS ONE, which is the assertion that actually failed first. No floor
    // sends TableView to its fallback - the viewport's own bottom edge - and the
    // strip lands under the plaque instead of above it, which looks like a
    // placement bug rather than a floor that was never computed.
    REQUIRE(floor.has_value());

    // ABOVE THE CARDS YOU ARE BIDDING ON, which in table space sit higher than
    // your plaque: C3's half of this, and the one the new layout turned round.
    REQUIRE(*floor <= tablespace::handTop);

    // And above your plaque, C2's half, bounded by the item itself rather than
    // by the scene's arithmetic.
    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);
    REQUIRE(*floor <= mine->sceneBoundingRect().top());
}

TEST_CASE("The strip has no floor before there is anything to place it against", "[table]")
{
    // No snapshot means no seats, so there is no plaque to clear - and the caller
    // is told so, and falls back, rather than being handed a number to lay a
    // widget out against.
    TableScene scene;

    REQUIRE_FALSE(scene.overlayBottom().has_value());
}

TEST_CASE("A theme change during a play prompt leaves the prompt's fan standing", "[table]")
{
    // THE TWO SOURCES MEETING AT THE WORST MOMENT. relayout() re-sources the fan
    // from the snapshot, and a theme change runs it - the system's colour scheme
    // can flip at any moment the user likes, while the engine is parked in
    // playCard() waiting to be told which card. Until the table redesign's phase
    // 2 every resize did the same, the View > Log toggle's included, which is the
    // case this was written for.
    //
    // seatedTable()'s snapshot carries no hand, which is not a gap in the fixture:
    // that is exactly the shape a Forehead or Hidden round publishes (section 17),
    // and a fan re-sourced from it here empties out. Nothing left to click, no
    // digit key in range, and an answer the game can never be given.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    PlayPrompt prompt;

    prompt.promptId = 5;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    scene.handItem()->setPrompt(prompt);
    scene.handItem()->setPlayable(true);

    scene.setTheme(Theme::dark());

    REQUIRE(scene.handItem()->slotCount() == 1);
    REQUIRE(scene.handItem()->isPlayable(0));
}

// ---- a card under a scaled view, added in the table redesign's phase 2 ----

// B1'S CONTRACT, WHICH THE SCALED VIEW WOULD HAVE BROKEN. CardRenderer rasterizes
// a card at exactly the size it is drawn, so the blit is 1:1 and the card is
// crisp. Under a view scaled to half, a pixmap rasterized at the card's TABLE size
// is shrunk by half on its way to the screen - resampled, which is the softness
// the renderer exists to avoid. So CardItem asks for the size the card really
// lands at, and what reaches the canvas is the renderer's own pixmap at that size,
// pixel for pixel.
TEST_CASE("A card under a scaled view is rasterized at the size it is drawn", "[table]")
{
    // Declared before the scene, which deletes its items: the renderer has to
    // outlive the card, as it does in TableScene.
    CardRenderer renderer;
    QGraphicsScene scene;

    const Card card(Rank::Queen, Suit::Hearts);

    auto* item = new CardItem(&renderer);
    item->setFace(card);
    item->setCardHeight(200.0);
    scene.addItem(item);

    const QSize full = renderer.sizeForHeight(200.0);
    const QRectF source = item->boundingRect();

    // Half size, which is the scale a viewport 680 pixels tall gives the table:
    // (680 - 18) / 1324, its margins taken off first.
    const QSize half(full.width() / 2, full.height() / 2);

    // An even width and height, so half of it is whole pixels and the card's
    // corner lands on one: otherwise the comparison is of a half-pixel offset.
    REQUIRE(full.width() % 2 == 0);
    REQUIRE(full.height() % 2 == 0);

    QImage rendered(half, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);

    {
        QPainter painter(&rendered);
        scene.render(&painter, QRectF(QPointF(0.0, 0.0), half), source);
    }

    // The renderer's pixmap copied 1:1. Its ratio is taken off first, because a
    // ratio of a half would have drawImage() lay it out at twice its pixels -
    // which is the scaling this case is checking the card did NOT need.
    QImage face = renderer.face(card, full, 0.5).toImage();
    face.setDevicePixelRatio(1.0);

    QImage expected(half, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);

    {
        QPainter painter(&expected);
        painter.drawImage(QPointF(0.0, 0.0), face);
    }

    // The premise: the pixmap the renderer hands back at a ratio of a half IS
    // half size, so matching it means the card was not resampled.
    REQUIRE(renderer.face(card, full, 0.5).size() == half);

    REQUIRE(rendered == expected);
}

// THE RATIO IS THE TARGET'S, not the widget's. A scene rendered somewhere other
// than a view - QGraphicsScene::render() into an image, as the case above does -
// paints with no widget, and CardItem used to read 1.0 there whatever the image's
// own ratio was. Into a ratio-2 image at half scale the card has to be rasterized
// at 2 x 0.5 = 1, its full size; reading the ratio as 1 gives a half-size pixmap
// stretched over twice its pixels.
TEST_CASE("A card rendered into a HiDPI image is rasterized at the image's ratio", "[table]")
{
    CardRenderer renderer;
    QGraphicsScene scene;

    const Card card(Rank::Queen, Suit::Hearts);

    auto* item = new CardItem(&renderer);
    item->setFace(card);
    item->setCardHeight(200.0);
    scene.addItem(item);

    const QSize full = renderer.sizeForHeight(200.0);
    const QRectF source = item->boundingRect();

    REQUIRE(full.width() % 2 == 0);
    REQUIRE(full.height() % 2 == 0);

    // Half the card in logical pixels, which at a ratio of 2 is all of it in
    // physical ones.
    const QSize half(full.width() / 2, full.height() / 2);

    QImage rendered(full, QImage::Format_ARGB32_Premultiplied);
    rendered.setDevicePixelRatio(2.0);
    rendered.fill(Qt::transparent);

    {
        QPainter painter(&rendered);
        scene.render(&painter, QRectF(QPointF(0.0, 0.0), half), source);
    }

    QImage expected(full, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);

    {
        QPainter painter(&expected);
        painter.drawImage(QPointF(0.0, 0.0), renderer.face(card, full, 1.0).toImage());
    }

    // Compared as pixels: the ratio was the input, not part of the answer.
    rendered.setDevicePixelRatio(1.0);

    REQUIRE(rendered == expected);
}

// PHASE5_REVIEW_FINDINGS.md #8. relayout() runs on every snapshot - about 1300 a
// game - and setRotation() repaints an item's whole area even when the angle is the
// one it already has. The racks, the played cards and the rhombus are all turned
// there, so a snapshot that changed nothing on the table repainted all of them, and
// the felt, the shadows and the panels beneath. Written for that, this also caught
// SeatItem::setSeat() repainting every panel, and your hand under yours.
TEST_CASE("A snapshot that changes nothing repaints nothing", "[table]")
{
    // BEFORE THE SCENE, so it outlives the connection that writes to it
    // (PHASE5_REVIEW_FINDINGS.md #20).
    QList<QRectF> repainted;

    TableScene scene;
    scene.setTheme(Theme::dark());

    // fullTable() is mid-trick: racks on the rim, five cards down, one of them
    // winning, and the rhombus on the felt - everything that is turned.
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));
    QCoreApplication::processEvents();

    QObject::connect(&scene, &QGraphicsScene::changed,
                     [&repainted](const QList<QRectF>& regions) { repainted += regions; });

    // The same table again, as a new frame: nothing on it has moved.
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));
    QCoreApplication::processEvents();

    REQUIRE(repainted.isEmpty());

    // The control: a frame that does change something is seen to.
    GameSnapshot moved = fullTable();
    moved.table.pop_back();
    scene.setSnapshot(std::make_shared<const GameSnapshot>(moved));
    QCoreApplication::processEvents();

    REQUIRE_FALSE(repainted.isEmpty());
}
