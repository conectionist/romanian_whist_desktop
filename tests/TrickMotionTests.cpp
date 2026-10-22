#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"
#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/Motion.h"
#include "table/SeatLayout.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"
#include "table/TableView.h"
#include "table/TrickItem.h"

#include <QCoreApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QVariantAnimation>

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

using Catch::Approx;
using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// A CARD FLIES TO THE FELT (the table redesign's phase 12): from the owner's panel
// for a bot, from the fan for you, worked out from two frames side by side.
//
// THE CLOCK IS THE TEST'S. Each flight is a QVariantAnimation, paused as soon as it
// is found and moved with setCurrentTime(): no sleeps, and no event loop turning
// that could move it on its own.
namespace
{

GameSnapshot tableOf(unsigned int seatCount, int humanSeat, std::vector<PlayedCardView> table = {},
                     std::vector<Card> hand = {}, unsigned int round = 1)
{
    GameSnapshot snapshot;
    snapshot.seats.resize(seatCount);

    for(unsigned int i = 0 ; i < seatCount ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].isHuman = static_cast<int>(i) == humanSeat;
    }

    snapshot.humanSeat = humanSeat;
    snapshot.roundNumber = round;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.betsPlaced = seatCount;
    snapshot.phase = romanian_whist::GamePhase::Playing;
    snapshot.table = std::move(table);

    for(const Card& card : hand)
        snapshot.hand.push_back(HandEntry{ card, true });

    return snapshot;
}

PlayedCardView playedBy(unsigned int seat, Card card)
{
    PlayedCardView entry;
    entry.seatIndex = seat;
    entry.card = card;
    return entry;
}

PlayedCardView playedBy(unsigned int seat, Rank rank)
{
    return playedBy(seat, Card(rank, Suit::Hearts));
}

void setTrickOf(TrickItem& trick, const GameSnapshot& snapshot, const TrickItem::HandPlace& fromHand = {})
{
    trick.setTrick(snapshot,
                   seatlayout::places(static_cast<unsigned int>(snapshot.seats.size()), snapshot.humanSeat),
                   Theme::dark(), fromHand);
}

std::vector<CardItem*> cardsOf(const QGraphicsItem& trick)
{
    std::vector<CardItem*> cards;

    for(QGraphicsItem* child : trick.childItems())
    {
        if(CardItem* card = dynamic_cast<CardItem*>(child))
            cards.push_back(card);
    }

    return cards;
}

// The flight of card `index`, taken off the event loop's clock.
QVariantAnimation* held(const TrickItem& trick, std::size_t index)
{
    QVariantAnimation* flight = trick.flight(index);

    if(flight)
        flight->pause();

    return flight;
}

// A trick item at Normal on a scene of its own.
struct LoneTrick
{
    CardRenderer renderer;
    QGraphicsScene scene;
    TrickItem* trick = new TrickItem(&renderer);

    LoneTrick()
    {
        scene.addItem(trick);
        trick->setPace(motion::Pace::Normal);
    }
};

}

TEST_CASE("A bot's card flies from its panel to its place", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    setTrickOf(trick, tableOf(4, 0));
    REQUIRE_FALSE(trick.isFlying());

    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }));

    QVariantAnimation* flight = held(trick, 0);
    REQUIRE(flight != nullptr);
    REQUIRE(flight->duration() == motion::flightDuration(motion::Pace::Normal));

    const seatlayout::Places places = seatlayout::places(4, 0);
    const tablespace::Placement landing = *trickitem::cardPlacement(4, 0, 1);
    CardItem* card = cardsOf(trick).at(0);

    // AT ITS OWNER'S PANEL, small and faint, already turned as it will lie.
    REQUIRE(card->pos() == places[1]->panel.center());
    REQUIRE(card->scale() == Approx(0.55));
    REQUIRE(card->opacity() == Approx(0.3));
    REQUIRE(card->rotation() == Approx(landing.rotation));

    // HALF THE TIME, MOST OF THE WAY: the curve eases out.
    flight->setCurrentTime(flight->duration() / 2);

    const qreal progress = (card->scale() - 0.55) / 0.45;
    REQUIRE(progress > 0.5);
    REQUIRE(progress < 1.0);
    REQUIRE(card->opacity() > 0.3);
    REQUIRE(card->opacity() < 1.0);

    // AND DOWN where a card at rest would be.
    flight->setCurrentTime(flight->duration());

    REQUIRE(trick.flight(0) == nullptr);
    REQUIRE_FALSE(trick.isFlying());
    REQUIRE(card->pos() == landing.centre);
    REQUIRE(card->rotation() == Approx(landing.rotation));
    REQUIRE(card->scale() == 1.0);
    REQUIRE(card->opacity() == 1.0);
}

// THE BRIDGE PUBLISHES THE NEXT SEAT'S TURN microseconds after a card lands, over
// the same felt. That frame must not cut the flight short, and a second card down
// while the first is still flying flies too.
TEST_CASE("A frame that takes nothing away leaves a flight running", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    setTrickOf(trick, tableOf(4, 0));
    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }));

    QVariantAnimation* first = held(trick, 0);
    REQUIRE(first != nullptr);
    first->setCurrentTime(100);

    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }));

    REQUIRE(trick.flight(0) == first);
    REQUIRE(first->currentTime() == 100);

    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven), playedBy(2, Rank::Nine) }));

    REQUIRE(trick.flight(0) == first);
    REQUIRE(trick.flight(1) != nullptr);
}

TEST_CASE("A frame that does not follow the last lands every flight", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    const auto launched = [&trick]
    {
        setTrickOf(trick, tableOf(4, 0, {}, {}, 3));
        setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }, {}, 3));
        REQUIRE(held(trick, 0) != nullptr);
    };

    SECTION("the felt cleared")
    {
        launched();
        setTrickOf(trick, tableOf(4, 0, {}, {}, 3));

        REQUIRE_FALSE(trick.isFlying());
        REQUIRE_FALSE(cardsOf(trick).at(0)->isVisible());
        REQUIRE(cardsOf(trick).at(0)->scale() == 1.0);
    }

    SECTION("a new round")
    {
        launched();
        setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }, {}, 4));

        REQUIRE_FALSE(trick.isFlying());
        REQUIRE(cardsOf(trick).at(0)->pos() == trickitem::cardPlacement(4, 0, 1)->centre);
    }

    SECTION("another card where the flying one was")
    {
        launched();
        setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Eight) }, {}, 3));

        REQUIRE_FALSE(trick.isFlying());
        REQUIRE(cardsOf(trick).at(0)->pos() == trickitem::cardPlacement(4, 0, 1)->centre);
        REQUIRE(cardsOf(trick).at(0)->opacity() == 1.0);
    }
}

TEST_CASE("At Off a card is placed at once", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;
    trick.setPace(motion::Pace::Off);

    setTrickOf(trick, tableOf(4, 0));
    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }));

    REQUIRE_FALSE(trick.isFlying());
    REQUIRE(cardsOf(trick).at(0)->pos() == trickitem::cardPlacement(4, 0, 1)->centre);
    REQUIRE(cardsOf(trick).at(0)->scale() == 1.0);
}

TEST_CASE("Turning motion off lands what is in the air", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    setTrickOf(trick, tableOf(4, 0));
    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }));
    REQUIRE(held(trick, 0) != nullptr);

    trick.setPace(motion::Pace::Off);

    REQUIRE_FALSE(trick.isFlying());
    REQUIRE(cardsOf(trick).at(0)->pos() == trickitem::cardPlacement(4, 0, 1)->centre);
    REQUIRE(cardsOf(trick).at(0)->opacity() == 1.0);
}

// The table as it first appears - a game joined mid-trick, or the window's first
// frame - has no frame before it to fly from.
TEST_CASE("The first frame has nothing to fly from", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Rank::Seven), playedBy(2, Rank::Nine) }));

    REQUIRE_FALSE(trick.isFlying());
}

// THE SLOWER PACE TAKES LONGER, and the faster shorter: the clock is set per flight.
TEST_CASE("A flight takes the pace's time", "[trick-motion]")
{
    for(const motion::Pace pace : { motion::Pace::Quick, motion::Pace::Normal, motion::Pace::Slow })
    {
        LoneTrick lone;
        lone.trick->setPace(pace);

        setTrickOf(*lone.trick, tableOf(4, 0));
        setTrickOf(*lone.trick, tableOf(4, 0, { playedBy(1, Rank::Seven) }));

        QVariantAnimation* flight = held(*lone.trick, 0);
        REQUIRE(flight != nullptr);
        REQUIRE(flight->duration() == motion::flightDuration(pace));
    }
}

// YOURS LEAVES THE FAN where it stood, at the fan's size, and passes over the fan's
// other cards on its way; the trick goes back under the fan once it is down.
TEST_CASE("Your card flies from where it stood in your fan", "[trick-motion]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setMotion(motion::Pace::Normal);

    const Card seven(Rank::Seven, Suit::Hearts);
    const Card nine(Rank::Nine, Suit::Spades);
    const Card king(Rank::King, Suit::Clubs);

    scene.setSnapshot(std::make_shared<const GameSnapshot>(tableOf(4, 0, {}, { seven, nine, king })));

    const std::optional<tablespace::Placement> stood = scene.handItem()->restingPlacement(nine);
    REQUIRE(stood.has_value());

    scene.setSnapshot(std::make_shared<const GameSnapshot>(tableOf(4, 0, { playedBy(0, nine) }, { seven, king })));

    const TrickItem* trick = scene.trickItem();
    QVariantAnimation* flight = held(*trick, 0);
    REQUIRE(flight != nullptr);

    CardItem* card = cardsOf(*trick).at(0);

    REQUIRE(card->pos() == stood->centre);
    REQUIRE(card->rotation() == Approx(stood->rotation));
    REQUIRE(card->scale() == Approx(tablespace::handCardHeight / tablespace::playedCardHeight(4)));
    REQUIRE(card->opacity() == 1.0);

    REQUIRE(trick->zValue() == tablespace::z::flyingCard);
    REQUIRE(trick->zValue() > tablespace::z::hand);
    REQUIRE(trick->zValue() < tablespace::z::humanPanel);

    flight->setCurrentTime(flight->duration());

    REQUIRE(card->pos() == trickitem::cardPlacement(4, 0, 0)->centre);
    REQUIRE(card->rotation() == Approx(0.0));
    REQUIRE(trick->zValue() == tablespace::z::playedCards);
}

// A BLIND ROUND'S FAN IS BACKS, and the card it held is the one played: the flight
// starts at the fan's only card.
TEST_CASE("Your blind card flies from the fan of backs", "[trick-motion]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setMotion(motion::Pace::Normal);

    scene.setSnapshot(std::make_shared<const GameSnapshot>(tableOf(4, 0)));

    PlayPrompt prompt;
    prompt.promptId = 3;
    prompt.blind = true;
    prompt.handIndex = { 0 };
    scene.handItem()->setPrompt(prompt);

    const QSizeF size = scene.cardRenderer().sizeForHeight(tablespace::handCardHeight);
    const tablespace::Placement only = handitem::cardPlacement(1, 0, size);

    scene.setSnapshot(std::make_shared<const GameSnapshot>(
        tableOf(4, 0, { playedBy(0, Card(Rank::Ace, Suit::Diamonds)) })));

    QVariantAnimation* flight = held(*scene.trickItem(), 0);
    REQUIRE(flight != nullptr);
    REQUIRE(cardsOf(*scene.trickItem()).at(0)->pos() == only.centre);
}

// A SCALED CARD IS DRAWN FROM THE PICTURE IT LANDS WITH. Asked for at each frame's
// own size, it would be rasterized again on every frame of its flight.
TEST_CASE("A card in flight is not rasterized again", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    setTrickOf(trick, tableOf(4, 0));
    setTrickOf(trick, tableOf(4, 0, { playedBy(1, Card(Rank::King, Suit::Spades)) }));

    QVariantAnimation* flight = held(trick, 0);
    REQUIRE(flight != nullptr);

    const QRectF all = tablespace::fitBox;

    const auto render = [&]
    {
        QImage image(QSize(940, 662), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        lone.scene.render(&painter, QRectF(QPointF(), QSizeF(image.size())), all);
    };

    flight->setCurrentTime(flight->duration() / 10);
    render();
    const std::size_t drawn = lone.renderer.rasterizations();

    for(int tenth : { 3, 5, 8 })
    {
        INFO("at " << tenth << " tenths");
        flight->setCurrentTime(flight->duration() * tenth / 10);
        render();
        REQUIRE(lone.renderer.rasterizations() == drawn);
    }
}

// THE FLIGHT IS IN TABLE SPACE: a window resized under it changes how big it is
// drawn, not where it goes.
TEST_CASE("A resize mid-flight lands the card in the right place", "[trick-motion]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setMotion(motion::Pace::Normal);

    TableView view(&scene);
    view.resize(1000, 700);
    view.show();

    scene.setSnapshot(std::make_shared<const GameSnapshot>(tableOf(4, 0)));
    scene.setSnapshot(std::make_shared<const GameSnapshot>(tableOf(4, 0, { playedBy(2, Rank::Seven) })));

    QVariantAnimation* flight = held(*scene.trickItem(), 0);
    REQUIRE(flight != nullptr);

    CardItem* card = cardsOf(*scene.trickItem()).at(0);

    flight->setCurrentTime(flight->duration() / 2);
    const QPointF halfway = card->pos();

    view.resize(1600, 900);
    QCoreApplication::processEvents();

    flight->setCurrentTime(flight->duration() / 2);
    REQUIRE(card->pos() == halfway);

    flight->setCurrentTime(flight->duration());
    REQUIRE(card->pos() == trickitem::cardPlacement(4, 0, 2)->centre);
}

// ---- the sweep (phase 12, step 5) ----

namespace
{
// A full trick of four in round `round`, seat `winner`'s card the engine's winner.
std::vector<PlayedCardView> fullTrick(unsigned int winner)
{
    std::vector<PlayedCardView> trick;

    for(unsigned int seat = 0 ; seat < 4 ; seat++)
    {
        PlayedCardView card = playedBy(seat, Card(static_cast<Rank>(seat + 2), Suit::Clubs));
        card.isWinning = seat == winner;
        trick.push_back(card);
    }

    return trick;
}

// A trick item at Normal that has shown the full trick, at rest, in round 5.
void dealtFull(TrickItem& trick, unsigned int winner)
{
    trick.setPace(motion::Pace::Off);
    setTrickOf(trick, tableOf(4, 0, fullTrick(winner), {}, 5));
    trick.setPace(motion::Pace::Normal);
}
}

TEST_CASE("A cleared trick sweeps to its winner's panel and goes", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    dealtFull(trick, 2);
    const std::vector<CardItem*> cards = cardsOf(trick);
    const QPointF start = cards.at(1)->pos();

    // THE CLEARING FRAME, and the next seat's turn straight after it: the same bare
    // felt twice, which must not cut the sweep short.
    setTrickOf(trick, tableOf(4, 0, {}, {}, 5));
    setTrickOf(trick, tableOf(4, 0, {}, {}, 5));

    REQUIRE(trick.isFlying());

    std::vector<QVariantAnimation*> sweeps;

    for(std::size_t i = 0 ; i < 4 ; i++)
    {
        QVariantAnimation* sweep = held(trick, i);
        REQUIRE(sweep != nullptr);
        REQUIRE(sweep->duration() == motion::sweepDuration(motion::Pace::Normal));
        REQUIRE(cards.at(i)->isVisible());
        sweeps.push_back(sweep);
    }

    // HALF THE TIME, LITTLE OF THE WAY (the curve eases in), and toward the winner.
    const QPointF target = seatlayout::places(4, 0)[2]->panel.center();

    sweeps[1]->setCurrentTime(sweeps[1]->duration() / 2);

    const qreal travelled = QLineF(start, cards.at(1)->pos()).length();
    const qreal whole = QLineF(start, target).length();

    REQUIRE(travelled > 0.0);
    REQUIRE(travelled < whole / 2.0);
    REQUIRE(QLineF(cards.at(1)->pos(), target).length() < whole);
    REQUIRE(cards.at(1)->opacity() < 1.0);

    for(QVariantAnimation* sweep : sweeps)
        sweep->setCurrentTime(sweep->duration());

    REQUIRE_FALSE(trick.isFlying());

    for(CardItem* card : cards)
    {
        REQUIRE_FALSE(card->isVisible());
        REQUIRE(card->scale() == 1.0);
        REQUIRE(card->opacity() == 1.0);
    }
}

TEST_CASE("The next trick's first card ends the sweep", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    dealtFull(trick, 2);
    setTrickOf(trick, tableOf(4, 0, {}, {}, 5));

    for(std::size_t i = 0 ; i < 4 ; i++)
        REQUIRE(held(trick, i) != nullptr);

    // The winner leads, and its card reuses the first item, which was being swept.
    setTrickOf(trick, tableOf(4, 0, { playedBy(2, Rank::Ace) }, {}, 5));

    const std::vector<CardItem*> cards = cardsOf(trick);

    REQUIRE(trick.flight(0) != nullptr);
    REQUIRE(cards.at(0)->isVisible());

    for(std::size_t i = 1 ; i < 4 ; i++)
    {
        INFO("card " << i);
        REQUIRE(trick.flight(i) == nullptr);
        REQUIRE_FALSE(cards.at(i)->isVisible());
    }

    // And the new card lands where it belongs, not where the sweep was taking it.
    held(trick, 0)->setCurrentTime(held(trick, 0)->duration());
    REQUIRE(cards.at(0)->pos() == trickitem::cardPlacement(4, 0, 2)->centre);
    REQUIRE(cards.at(0)->opacity() == 1.0);
}

// THE ROUND'S LAST TRICK STAYS UNDER THE ROUND SHEET, and the next round's bare felt
// clears it at once.
TEST_CASE("No sweep across rounds", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    dealtFull(trick, 1);
    setTrickOf(trick, tableOf(4, 0, {}, {}, 6));

    REQUIRE_FALSE(trick.isFlying());

    for(CardItem* card : cardsOf(trick))
        REQUIRE_FALSE(card->isVisible());
}

TEST_CASE("No sweep at Off", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    dealtFull(trick, 1);
    trick.setPace(motion::Pace::Off);
    setTrickOf(trick, tableOf(4, 0, {}, {}, 5));

    REQUIRE_FALSE(trick.isFlying());

    for(CardItem* card : cardsOf(trick))
        REQUIRE_FALSE(card->isVisible());
}

TEST_CASE("Turning motion off mid-sweep takes the cards off at once", "[trick-motion]")
{
    LoneTrick lone;
    TrickItem& trick = *lone.trick;

    dealtFull(trick, 3);
    setTrickOf(trick, tableOf(4, 0, {}, {}, 5));
    REQUIRE(held(trick, 0) != nullptr);

    trick.setPace(motion::Pace::Off);

    REQUIRE_FALSE(trick.isFlying());

    for(CardItem* card : cardsOf(trick))
        REQUIRE_FALSE(card->isVisible());
}
