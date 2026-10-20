#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameFixtures.h"

#include "paint/CardRenderer.h"
#include "paint/CardShadow.h"
#include "paint/Shadows.h"
#include "paint/Theme.h"
#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/TableSpace.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QMarginsF>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace romanian_whist;

// The fan, and the two things about it that are not obvious.
//
// THE FIRST IS THE HIT TEST. The cards overlap by about half, so most of the fan
// is covered by its neighbour - and the card a player is pointing at is the LAST
// one that contains the cursor, not the first. Walking forwards returns the card
// underneath, and every click in an overlap plays a neighbour. That is the same
// defect PlayPrompt::handIndex exists to prevent, arriving from the geometry
// instead of from the indexing. Since the redesign's phase 7 every card is turned,
// so "contains" is asked in each card's own frame.
//
// THE SECOND IS THAT THE FAN HAS TWO SOURCES. Between turns it is the snapshot's
// hand and is never playable; on your turn it is the prompt, with legality that
// means something - or, in a blind round, a count of face-down backs and nothing
// else (sections 4.0 and 17).

namespace
{
// The table's own: section 2.8's steps and arc are fixed numbers, so at another
// size the cards would no longer overlap the way the mock-up's do.
constexpr qreal kCardHeight = tablespace::handCardHeight;

const Card SpadesAce(Rank::Ace, Suit::Spades);
const Card HeartsKing(Rank::King, Suit::Hearts);
const Card DiamondsSeven(Rank::Seven, Suit::Diamonds);

std::vector<HandEntry> handOf(std::vector<bool> legality)
{
    const std::vector<Card> cards{ SpadesAce, HeartsKing, DiamondsSeven };

    std::vector<HandEntry> entries;

    for(std::size_t i = 0 ; i < legality.size() ; i++)
        entries.push_back(HandEntry{ cards[i % cards.size()], legality[i] });

    return entries;
}

PlayPrompt promptOf(std::vector<bool> legality)
{
    PlayPrompt prompt;
    prompt.promptId = 7;
    prompt.entries = handOf(legality);

    // Deliberately NOT the identity, for the reason the C1 tests give: a caller
    // that returned a hand position where a display slot was wanted would agree
    // with the identity and with nothing else.
    for(std::size_t i = 0 ; i < legality.size() ; i++)
        prompt.handIndex.push_back(legality.size() - 1 - i);

    return prompt;
}

// A fan built and ready to be asked about.
//
// DECLARATION ORDER IS LOAD-BEARING TWICE OVER, and members are destroyed in
// reverse of it. `item` holds a pointer to `renderer`, so the renderer must
// outlive it; and `item` is added to `scene`, so it has to be destroyed while
// the scene still exists - a QGraphicsItem removes itself from its scene on the
// way out, and a scene destroyed first would delete a member instead.
struct Fanned
{
    QGraphicsScene scene;
    CardRenderer renderer;
    HandItem item{ &renderer };

    explicit Fanned(const PlayPrompt& prompt, bool playable = true)
    {
        item.setCardHeight(kCardHeight);
        item.setPrompt(prompt);
        item.setPlayable(playable);

        scene.addItem(&item);
    }

    // A CLICK AT A POINT, which is the only way to reach slotAt() - it is private,
    // and it should be: nothing outside the item has any business asking which
    // card is under a pixel. Going through the real event is also the only
    // version of this test that would notice the hit test walking the wrong way.
    void clickAt(QPointF where)
    {
        QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);

        event.setPos(where);
        event.setButton(Qt::LeftButton);

        scene.sendEvent(&item, &event);
    }

    // The cursor moving to a point, which is how a card comes to be raised.
    void hoverAt(QPointF where)
    {
        QGraphicsSceneHoverEvent event(QEvent::GraphicsSceneHoverMove);

        event.setPos(where);

        scene.sendEvent(&item, &event);
    }

    // The cursor leaving the fan.
    void leave()
    {
        QGraphicsSceneHoverEvent event(QEvent::GraphicsSceneHoverLeave);

        scene.sendEvent(&item, &event);
    }

    std::vector<CardItem*> cards() const
    {
        return item.childItems().isEmpty() ? std::vector<CardItem*>{} : collect();
    }

private:
    std::vector<CardItem*> collect() const
    {
        std::vector<CardItem*> found;

        for(QGraphicsItem* child : item.childItems())
        {
            if(auto* card = dynamic_cast<CardItem*>(child))
                found.push_back(card);
        }

        return found;
    }
};

// Whether a point in the fan's coordinates is on the card as it rests: its own
// rect, not boundingRect(), which reaches past the card for its shadow.
bool onCard(const CardItem& card, QPointF where)
{
    return card.cardRect().contains(card.mapFromParent(where));
}

bool anyRaised(const std::vector<CardItem*>& cards)
{
    return std::any_of(cards.begin(), cards.end(), [](const CardItem* card) { return card->isRaised(); });
}

// TABLE.JS'S OWN TRANSFORM FOR A CARD, written out the way CSS builds it rather than
// the way HandItem does: the card's box at (x, y), turned about
// transform-origin: 50% 130% - so `translate(x, y) rotate(rot)` with the origin
// moved there and back. It maps a point measured from the card's top-left corner.
// renderHand(): step = min(84, (720 - CW) / (n - 1)), x = 768 - total / 2 + i * step,
// y = 850 + (i - mid)^2 * 1.6, rot = (i - mid) * 1.9deg.
QTransform cssTransform(std::size_t count, std::size_t slot, QSizeF card)
{
    const qreal n = static_cast<qreal>(count);
    const qreal i = static_cast<qreal>(slot);
    const qreal step = count > 1 ? std::min(84.0, (720.0 - card.width()) / (n - 1.0)) : 0.0;
    const qreal total = (n - 1.0) * step + card.width();
    const qreal mid = (n - 1.0) / 2.0;

    const QPointF box(768.0 - total / 2.0 + i * step, 850.0 + (i - mid) * (i - mid) * 1.6);
    const QPointF origin(card.width() * 0.5, card.height() * 1.3);

    QTransform css;
    css.translate(box.x() + origin.x(), box.y() + origin.y());
    css.rotate((i - mid) * 1.9);
    css.translate(-origin.x(), -origin.y());

    return css;
}
}

TEST_CASE("The fan draws one card per slot where cardPlacement() puts it", "[hand]")
{
    Fanned fan(promptOf({ true, true, true, true, true }));

    REQUIRE(fan.item.slotCount() == 5);

    const std::vector<CardItem*> cards = fan.cards();

    REQUIRE(cards.size() == 5);

    for(std::size_t i = 0 ; i < cards.size() ; i++)
    {
        INFO("slot " << i);

        const tablespace::Placement place = handitem::cardPlacement(5, i, cards[i]->cardSize());

        REQUIRE(cards[i]->pos().x() == Catch::Approx(place.centre.x()));
        REQUIRE(cards[i]->pos().y() == Catch::Approx(place.centre.y()));
        REQUIRE(cards[i]->rotation() == Catch::Approx(place.rotation).margin(1e-12));

        // LATER CARDS ON TOP, the order slotAt() walks backwards.
        REQUIRE(cards[i]->zValue() == static_cast<qreal>(i));
    }
}

TEST_CASE("The fan is mirrored about the middle of the table", "[hand]")
{
    // Section 14: "the fan is centred". Every card's mirror image across x = 768 is
    // the card as many slots from the other end, turned the other way - for every
    // size of hand, odd and even.
    CardRenderer renderer;
    const QSizeF card = renderer.sizeForHeight(kCardHeight);

    for(std::size_t count = 1 ; count <= 8 ; count++)
    {
        for(std::size_t i = 0 ; i < count ; i++)
        {
            INFO(count << " cards, slot " << i);

            const tablespace::Placement place = handitem::cardPlacement(count, i, card);
            const tablespace::Placement mirror = handitem::cardPlacement(count, count - 1 - i, card);

            REQUIRE(place.centre.x() - tablespace::centre.x()
                    == Catch::Approx(tablespace::centre.x() - mirror.centre.x()).margin(1e-9));
            REQUIRE(place.centre.y() == Catch::Approx(mirror.centre.y()));
            REQUIRE(place.rotation == Catch::Approx(-mirror.rotation).margin(1e-12));
        }
    }

    // The middle card of an odd hand is square to the table, its top at section
    // 2.8's 850.
    const tablespace::Placement middle = handitem::cardPlacement(5, 2, card);

    REQUIRE(middle.rotation == 0.0);
    REQUIRE(middle.centre.x() == Catch::Approx(tablespace::centre.x()));
    REQUIRE(middle.centre.y() - card.height() / 2.0 == Catch::Approx(tablespace::handTop));
}

TEST_CASE("Each card in the fan stands where table.js puts it", "[hand]")
{
    // THE HINGE, CHECKED AGAINST CSS. HandItem turns each card about its own middle
    // and moves the middle to where turning about the hinge would have carried it;
    // cssTransform() turns about the hinge directly, as the browser does. Every
    // corner of every card has to land on the same point both ways.
    for(const std::size_t count : { std::size_t(1), std::size_t(4), std::size_t(8) })
    {
        Fanned fan(promptOf(std::vector<bool>(count, true)));

        const std::vector<CardItem*> cards = fan.cards();

        REQUIRE(cards.size() == count);

        for(std::size_t i = 0 ; i < count ; i++)
        {
            const QSizeF size = cards[i]->cardSize();
            const QTransform css = cssTransform(count, i, size);

            const std::array<QPointF, 4> corners{ QPointF(0.0, 0.0), QPointF(size.width(), 0.0),
                                                  QPointF(size.width(), size.height()),
                                                  QPointF(0.0, size.height()) };

            for(const QPointF& corner : corners)
            {
                INFO(count << " cards, slot " << i << ", corner " << corner.x() << ", " << corner.y());

                const QPointF ours = cards[i]->mapToParent(corner - QPointF(size.width() / 2.0,
                                                                            size.height() / 2.0));
                const QPointF theirs = css.map(corner);

                REQUIRE(ours.x() == Catch::Approx(theirs.x()).margin(1e-6));
                REQUIRE(ours.y() == Catch::Approx(theirs.y()).margin(1e-6));
            }
        }
    }
}

TEST_CASE("The fan steps 84 apart until eight cards close it to 720", "[hand]")
{
    // THE OVERLAP, measured where it is set: at each card's hinge, 1.3 card heights
    // below its top and under its middle, which the turn leaves where it was. The
    // step is section 2.8's 84 up to seven cards, and a full hand of eight closes up
    // so the fan is 720 wide in all. Either way a card covers part of the one
    // before it - without that a full hand would not fit the rim.
    CardRenderer renderer;
    const QSizeF card = renderer.sizeForHeight(kCardHeight);

    const auto hingeOf = [&card](const tablespace::Placement& place)
    {
        const qreal arm = 0.8 * card.height();
        const qreal turn = qDegreesToRadians(place.rotation);

        return place.centre - QPointF(arm * std::sin(turn), -arm * std::cos(turn));
    };

    for(std::size_t count = 2 ; count <= 8 ; count++)
    {
        INFO(count << " cards");

        const qreal first = hingeOf(handitem::cardPlacement(count, 0, card)).x();
        const qreal second = hingeOf(handitem::cardPlacement(count, 1, card)).x();
        const qreal last = hingeOf(handitem::cardPlacement(count, count - 1, card)).x();

        const qreal step = second - first;

        REQUIRE(step < card.width());

        if(count < 8)
            REQUIRE(step == Catch::Approx(84.0));
        else
            REQUIRE(last - first + card.width() == Catch::Approx(720.0));
    }
}

TEST_CASE("A card wider than the fan stacks the hand rather than reversing it", "[hand]")
{
    // (720 - 800) / 7 is a NEGATIVE step: each card would sit left of the one before
    // it and still be drawn on top of it, and the hit test's "the last match is the
    // topmost card" would play the card underneath. Clamped, the hand stacks.
    const QSizeF card(800.0, 1100.0);

    const auto hingeX = [&card](std::size_t slot)
    {
        const tablespace::Placement place = handitem::cardPlacement(8, slot, card);
        const qreal arm = 0.8 * card.height();

        return place.centre.x() - arm * std::sin(qDegreesToRadians(place.rotation));
    };

    for(std::size_t slot = 1 ; slot < 8 ; slot++)
    {
        INFO("slot " << slot);

        REQUIRE(hingeX(slot) == Catch::Approx(hingeX(0)));
    }
}

TEST_CASE("The slot under an overlap is the upper card, not the lower", "[hand]")
{
    // THE CASE THIS FILE EXISTS FOR. Card i+1 covers the right part of card i, so
    // a point inside that strip is on BOTH cards - and the one the player can see,
    // and therefore the one they mean, is i+1. A forward walk returns i.
    Fanned fan(promptOf({ true, true, true }));

    const std::vector<CardItem*> cards = fan.cards();

    REQUIRE(cards.size() == 3);

    // Just inside the left edge of the middle card, half way down it, which is
    // still over the first one.
    const QSizeF size = cards[1]->cardSize();
    const QPointF where = cards[1]->mapToParent(QPointF(-size.width() / 2.0 + 2.0, 0.0));

    // THE PREMISE, asserted rather than assumed: this point really is on both
    // cards. Without that the case would pass on a fan that did not overlap at all
    // and would be saying nothing.
    REQUIRE(onCard(*cards[0], where));
    REQUIRE(onCard(*cards[1], where));

    std::vector<std::size_t> clicked;

    QObject::connect(&fan.item, &HandItem::cardClicked,
                     [&clicked](std::size_t slot){ clicked.push_back(slot); });

    // THROUGH A REAL EVENT, so the private hit test actually runs. Calling
    // activate(1) here would assert only that the signal carries what it was
    // handed - which it does whichever way slotAt() walks, and that version of
    // this case passed the mutation it was written to catch.
    fan.clickAt(where);

    REQUIRE(clicked.size() == 1);
    REQUIRE(clicked.front() == 1);
}

TEST_CASE("A raised card stays raised over the strip it rose from", "[hand]")
{
    // THE FLICKER. A card rises 30 under the cursor, so a cursor near its bottom
    // edge is off the card once it has risen. Asked about the risen card alone, the
    // next move finds nothing there and drops it, the move after finds it at rest
    // and raises it again. The mock-up's browser hovers the card's resting place as
    // well as its risen face, and so does the fan.
    Fanned fan(promptOf({ true, true, true }));

    const std::vector<CardItem*> cards = fan.cards();
    const QSizeF size = cards[0]->cardSize();

    // Near the bottom of the first card, left of where the second covers it.
    const QPointF where = cards[0]->mapToParent(QPointF(-size.width() / 4.0, size.height() / 2.0 - 10.0));

    // The premise: no other card is there, and the risen card does not reach down
    // to it.
    REQUIRE(onCard(*cards[0], where));
    REQUIRE_FALSE(onCard(*cards[1], where));
    REQUIRE_FALSE(cards[0]->cardRect().translated(0.0, -cardshadow::raisedLift)
                      .contains(cards[0]->mapFromParent(where)));

    fan.hoverAt(where);

    REQUIRE(cards[0]->isRaised());

    fan.hoverAt(where);

    REQUIRE(cards[0]->isRaised());

    // And a click there plays it.
    std::vector<std::size_t> clicked;

    QObject::connect(&fan.item, &HandItem::cardClicked,
                     [&clicked](std::size_t slot){ clicked.push_back(slot); });

    fan.clickAt(where);

    REQUIRE(clicked == std::vector<std::size_t>{ 0 });
}

TEST_CASE("A raised card rises along its own tilt and is hit where it rose to", "[hand]")
{
    Fanned fan(promptOf({ true, true, true }));

    const std::vector<CardItem*> cards = fan.cards();
    const QSizeF size = cards[0]->cardSize();

    // The first card of three is turned 1.9 degrees anticlockwise, so its own up
    // leans left.
    REQUIRE(cards[0]->rotation() == Catch::Approx(-1.9));

    // Just above the card's resting top: in the strip it rises into, and on no card
    // until it does.
    const QPointF above = cards[0]->mapToParent(QPointF(-size.width() / 4.0, -size.height() / 2.0 - 15.0));

    for(const CardItem* card : cards)
        REQUIRE_FALSE(onCard(*card, above));

    std::vector<std::size_t> clicked;

    QObject::connect(&fan.item, &HandItem::cardClicked,
                     [&clicked](std::size_t slot){ clicked.push_back(slot); });

    fan.clickAt(above);

    REQUIRE(clicked.empty());

    // Raised from its resting place, the card covers the point, and a click there
    // plays it.
    fan.hoverAt(cards[0]->mapToParent(QPointF(-size.width() / 4.0, 0.0)));

    REQUIRE(cards[0]->isRaised());

    fan.clickAt(above);

    REQUIRE(clicked == std::vector<std::size_t>{ 0 });

    // THE RISE IS THE CARD'S OWN UP, turned with it: 30 along it, which leans left
    // of straight up by the card's turn.
    const QPointF rise = cards[0]->mapToParent(QPointF(0.0, -cardshadow::raisedLift))
                         - cards[0]->mapToParent(QPointF(0.0, 0.0));
    const qreal turn = qDegreesToRadians(cards[0]->rotation());

    REQUIRE(rise.x() == Catch::Approx(cardshadow::raisedLift * std::sin(turn)));
    REQUIRE(rise.y() == Catch::Approx(-cardshadow::raisedLift * std::cos(turn)));

    // The card has not moved: the rise is drawn.
    REQUIRE(cards[0]->pos().x() == Catch::Approx(handitem::cardPlacement(3, 0, size).centre.x()));
    REQUIRE(cards[0]->pos().y() == Catch::Approx(handitem::cardPlacement(3, 0, size).centre.y()));
}

TEST_CASE("The next card still wins where it covers the raised card's risen face", "[hand]")
{
    // THE ORDER INSIDE slotAt(): each card's resting place first, walking down from
    // the top, and the raised card's risen part only at its own turn. The card above
    // it is drawn over its risen face, so a point on both is that card's. Testing the
    // risen part first - for the raised card, or for every card - would hand it the
    // raised one, and no other case here would notice.
    Fanned fan(promptOf({ true, true, true }));

    const std::vector<CardItem*> cards = fan.cards();
    const QSizeF size = cards[0]->cardSize();

    // Raised from a point on the first card alone.
    fan.hoverAt(cards[0]->mapToParent(QPointF(-size.width() / 4.0, 0.0)));

    REQUIRE(cards[0]->isRaised());

    // Just inside the second card's left edge, half way down.
    const QPointF where = cards[1]->mapToParent(QPointF(-size.width() / 2.0 + 2.0, 0.0));

    // The premise: the point is on the second card and on the first one's risen face.
    REQUIRE(onCard(*cards[1], where));
    REQUIRE(cards[0]->cardRect().translated(0.0, -cardshadow::raisedLift)
                .contains(cards[0]->mapFromParent(where)));

    std::vector<std::size_t> clicked;

    QObject::connect(&fan.item, &HandItem::cardClicked,
                     [&clicked](std::size_t slot){ clicked.push_back(slot); });

    fan.clickAt(where);

    REQUIRE(clicked == std::vector<std::size_t>{ 1 });

    // And the cursor moving there raises the second card in the first one's place.
    fan.hoverAt(where);

    REQUIRE(cards[1]->isRaised());
    REQUIRE_FALSE(cards[0]->isRaised());
}

TEST_CASE("Only a playable card is raised", "[hand]")
{
    // A dimmed card rising under the cursor says it can be clicked, which is exactly
    // what it cannot.
    Fanned fan(promptOf({ false, true, false }));

    const std::vector<CardItem*> cards = fan.cards();
    const QSizeF size = cards[0]->cardSize();

    const auto noneRaised = [&cards]
    {
        return std::none_of(cards.begin(), cards.end(), [](const CardItem* card) { return card->isRaised(); });
    };

    // The illegal first card, left of where the second covers it.
    fan.hoverAt(cards[0]->mapToParent(QPointF(-size.width() / 4.0, 0.0)));

    REQUIRE(noneRaised());

    fan.hoverAt(cards[1]->mapToParent(QPointF(0.0, 0.0)));

    REQUIRE(cards[1]->isRaised());

    // A hand that stops being playable drops the card it had raised.
    fan.item.setPlayable(false);

    REQUIRE(noneRaised());

    // And every card, raised or not, casts the hand's shadow.
    for(const CardItem* card : cards)
        REQUIRE(card->look() == cardshadow::Look::Held);
}

TEST_CASE("A card under a still cursor rises when the hand becomes playable", "[hand]")
{
    // YOUR TURN ARRIVING WHILE YOU REST ON A CARD. MainWindow arms the fan a line
    // after setPrompt() rebuilds it, and neither is a mouse move, so the scene sends
    // no hover: without the fan remembering the cursor, the card under it stayed
    // down until the mouse was nudged. The mock-up's browser raises it at once.
    Fanned fan(promptOf({ false, true, false }), false);

    const std::vector<CardItem*> cards = fan.cards();
    const QSizeF size = cards[0]->cardSize();

    fan.hoverAt(cards[1]->mapToParent(QPointF(0.0, 0.0)));

    REQUIRE_FALSE(anyRaised(cards));

    fan.item.setPlayable(true);

    REQUIRE(cards[1]->isRaised());

    // An illegal card under a still cursor stays down, as under a moving one.
    fan.item.setPlayable(false);
    fan.hoverAt(cards[0]->mapToParent(QPointF(-size.width() / 4.0, 0.0)));
    fan.item.setPlayable(true);

    REQUIRE_FALSE(anyRaised(cards));
}

TEST_CASE("A fan rebuilt under a still cursor raises the card under it again", "[hand]")
{
    // A REBUILD IS NOT A MOUSE MOVE EITHER. A theme change replaces every card, and
    // the card the cursor rests on has to rise again without waiting for the mouse.
    Fanned fan(promptOf({ true, true, true }));

    fan.hoverAt(fan.cards()[1]->mapToParent(QPointF(0.0, 0.0)));

    REQUIRE(fan.cards()[1]->isRaised());

    fan.item.setTheme(Theme::dark());

    const std::vector<CardItem*> rebuilt = fan.cards();

    REQUIRE(rebuilt.size() == 3);
    REQUIRE(rebuilt[1]->isRaised());
}

TEST_CASE("A cursor that has left the fan raises nothing when the fan changes", "[hand]")
{
    // The other half of remembering the cursor: forgetting it once it has gone.
    Fanned fan(promptOf({ true, true, true }));

    fan.hoverAt(fan.cards()[1]->mapToParent(QPointF(0.0, 0.0)));

    REQUIRE(fan.cards()[1]->isRaised());

    fan.leave();

    REQUIRE_FALSE(anyRaised(fan.cards()));

    // The next turn, as MainWindow deals it.
    fan.item.setPlayable(false);
    fan.item.setPrompt(promptOf({ true, true, true }));
    fan.item.setPlayable(true);

    REQUIRE_FALSE(anyRaised(fan.cards()));
}

TEST_CASE("An illegal slot is not playable and a legal one is", "[hand]")
{
    Fanned fan(promptOf({ false, true, false }));

    REQUIRE_FALSE(fan.item.isPlayable(0));
    REQUIRE(fan.item.isPlayable(1));
    REQUIRE_FALSE(fan.item.isPlayable(2));

    // Out of range is not playable either, which is what keeps the digit keys
    // from indexing past the end of a short hand.
    REQUIRE_FALSE(fan.item.isPlayable(9));

    std::vector<std::size_t> clicked;

    QObject::connect(&fan.item, &HandItem::cardClicked,
                     [&clicked](std::size_t slot){ clicked.push_back(slot); });

    fan.item.activate(0);
    fan.item.activate(2);

    REQUIRE(clicked.empty());

    fan.item.activate(1);

    REQUIRE(clicked.size() == 1);
    REQUIRE(clicked.front() == 1);
}

TEST_CASE("A hand that is not playable refuses every slot", "[hand]")
{
    // BETWEEN TURNS, which is most of a game. Section 4.4: the fan is drawn from
    // the snapshot and DISABLED, always - a snapshot carries no legality, so
    // every entry reads legal and only this flag stands between a stray click and
    // a submission.
    Fanned fan(promptOf({ true, true, true }), false);

    REQUIRE(fan.item.slotCount() == 3);
    REQUIRE_FALSE(fan.item.isPlayable(1));
}

TEST_CASE("A blind prompt fans backs, playable, with no card shown", "[hand]")
{
    // SECTION 17, asserted on the item. The hand is not sent and then not drawn -
    // it is not sent - so all the fan has is a count of positions. And the back
    // is still a button: a widget that drew nothing clickable here stalls the
    // last two rounds of every default game.
    PlayPrompt prompt;
    prompt.promptId = 11;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    REQUIRE(prompt.entries.empty());

    Fanned fan(prompt);

    REQUIRE(fan.item.slotCount() == 1);
    REQUIRE(fan.item.isPlayable(0));
}

TEST_CASE("A shorter hand leaves no cards from the longer one", "[hand]")
{
    // The hand loses a card every trick, so nearly every rebuild is smaller than
    // the last. The same trap BidStrip has a case for, one layer down: a child
    // item that was hidden rather than deleted still belongs to the parent and
    // still answers a hit test.
    Fanned fan(promptOf({ true, true, true, true, true, true }));

    REQUIRE(fan.cards().size() == 6);

    fan.item.setPrompt(promptOf({ true, true }));

    REQUIRE(fan.item.slotCount() == 2);
    REQUIRE(fan.cards().size() == 2);
    REQUIRE_FALSE(fan.item.isPlayable(5));
}

TEST_CASE("A snapshot hand replaces a prompt's, and is never playable", "[hand]")
{
    // THE TWO SOURCES, in the order a real turn runs them: the prompt arrives,
    // the card is played, and the next snapshot draws what is left. The second
    // must not inherit the first's legality - every entry on a snapshot reads
    // legal, and a fan still playable from the last prompt is a live hand while
    // a bot is thinking.
    Fanned fan(promptOf({ true, false, true }));

    REQUIRE(fan.item.isPlayable(0));

    fan.item.setPlayable(false);
    fan.item.setHand(handOf({ true, true }));

    REQUIRE(fan.item.slotCount() == 2);
    REQUIRE_FALSE(fan.item.isPlayable(0));
}

TEST_CASE("A snapshot arriving mid-prompt does not take the fan", "[hand]")
{
    // THE RESIZE THAT HUNG THE GAME. TableScene::relayout() re-sources the fan
    // from the snapshot, and it runs on every theme change - and, until the table
    // redesign's phase 2, on every viewport resize - including while the engine
    // is parked in playCard() waiting for this fan to be answered. This is that
    // call, made at that moment.
    //
    // The hand below holds the same three cards the prompt does and calls them
    // all legal, which is what every snapshot hand says. Taking it would light up
    // the card the prompt dimmed.
    Fanned fan(promptOf({ true, false, true }));

    fan.item.setHand(handOf({ true, true, true }));

    REQUIRE(fan.item.slotCount() == 3);
    REQUIRE(fan.item.isPlayable(0));
    REQUIRE_FALSE(fan.item.isPlayable(1));
}

TEST_CASE("A snapshot arriving mid-prompt leaves a blind round's backs standing", "[hand]")
{
    // THE SAME CALL, in the round where it cost the whole game rather than the
    // dimming: a Forehead or Hidden round sends no hand at all, so the snapshot
    // hand a relayout would draw from is EMPTY. A fan emptied here has no card to
    // click and no digit key in range, and the engine waits for an answer that
    // can no longer be given (sections 4.0 and 17).
    PlayPrompt prompt;

    prompt.promptId = 11;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    Fanned fan(prompt);

    fan.item.setHand({});

    REQUIRE(fan.item.slotCount() == 1);
    REQUIRE(fan.item.isPlayable(0));
}

TEST_CASE("The same snapshot hand twice does not rebuild the fan", "[hand]")
{
    // About 1300 snapshots reach the fan in a game and most carry a hand that has
    // not changed since the last one - every frame between two plays is one - and
    // every resize frame arrives here too. Rebuilding for them deletes and re-news
    // every CardItem, which is the churn TableScene's class comment rules out for
    // the seats and the trick, and from 18.2 on it is a card deleted out from
    // under the animation moving it.
    Fanned fan(promptOf({ true, true, true }));

    fan.item.setPlayable(false);
    fan.item.setHand(handOf({ true, true, true, true }));

    std::vector<CardItem*> built = fan.cards();

    REQUIRE(built.size() == 4);

    // MARKED RATHER THAN COMPARED BY ADDRESS. A rebuild deletes these and news
    // the replacements straight away, so the allocator may well hand back the
    // same pointers - a test that compared them could pass over the very churn it
    // is here to catch. Nothing carries a mark across a delete.
    for(CardItem* card : built)
        card->setData(0, QStringLiteral("kept"));

    fan.item.setHand(handOf({ true, true, true, true }));

    for(CardItem* card : fan.cards())
        REQUIRE(card->data(0).toString() == QStringLiteral("kept"));

    // AND A HAND THAT HAS CHANGED still rebuilds, which is the half a guard like
    // this quietly breaks: the fan loses a card every trick.
    fan.item.setHand(handOf({ true, true, true }));

    REQUIRE(fan.item.slotCount() == 3);

    for(CardItem* card : fan.cards())
        REQUIRE(card->data(0).toString().isEmpty());
}

// ---- what a card in your hand casts (section 2.8) ----
//
// page.html's .hcard .face: a shadow down and to the left at rest, and under the
// cursor a deeper one below, a glow and the gold-bright ring, the card risen 30
// along its own tilt.

TEST_CASE("A card in your hand casts its shadow down and to the left", "[hand]")
{
    // -3px 4px 10px: the blur reaches as far as shadows::reach(10) round a shadow
    // moved 3 left and 4 down, and a unit more for the anti-aliased edge.
    const qreal blur = shadows::reach(10.0);
    const QMarginsF reach = cardshadow::reach(cardshadow::Look::Held);

    REQUIRE(reach.left() == Catch::Approx(blur + 3.0 + 1.0));
    REQUIRE(reach.top() == Catch::Approx(blur - 4.0 + 1.0));
    REQUIRE(reach.right() == Catch::Approx(blur - 3.0 + 1.0));
    REQUIRE(reach.bottom() == Catch::Approx(blur + 4.0 + 1.0));

    // It does not rise: only the card under the cursor does.
    REQUIRE(cardshadow::lift(cardshadow::Look::Held) == 0.0);

    // And nothing under the card: CSS clips an outer shadow to outside the element.
    const Theme theme = Theme::dark();
    const cardshadow::Picture picture =
        cardshadow::render(QSizeF(155.0, 225.0), cardshadow::Look::Held, cardshadow::colours(theme.table), 1.0);

    REQUIRE_FALSE(picture.pixmap.isNull());
    REQUIRE(qAlpha(picture.pixmap.toImage().pixel((-picture.offset).toPoint())) == 0);
}

TEST_CASE("A raised card in your hand rises 30 and glows inside the ring", "[hand]")
{
    REQUIRE(cardshadow::lift(cardshadow::Look::Raised) == 30.0);

    // 0 0 22px of glow on every side, the 0 14px 22px shadow further below, and a
    // unit for the edge. The 2px ring is inside the glow's reach.
    const qreal blur = shadows::reach(22.0);
    const QMarginsF reach = cardshadow::reach(cardshadow::Look::Raised);

    REQUIRE(reach.left() == Catch::Approx(blur + 1.0));
    REQUIRE(reach.top() == Catch::Approx(blur + 1.0));
    REQUIRE(reach.right() == Catch::Approx(blur + 1.0));
    REQUIRE(reach.bottom() == Catch::Approx(blur + 14.0 + 1.0));

    // The ring is filled through under the card, as the winner's is, so the art
    // meets one edge.
    const Theme theme = Theme::dark();
    const cardshadow::Picture picture =
        cardshadow::render(QSizeF(155.0, 225.0), cardshadow::Look::Raised, cardshadow::colours(theme.table), 1.0);

    REQUIRE_FALSE(picture.pixmap.isNull());
    REQUIRE(picture.pixmap.toImage().pixelColor((-picture.offset).toPoint()) == theme.table.goldBright);
}

TEST_CASE("A card in your hand reaches as far as its look", "[hand]")
{
    // CardItem's rect is what the scene repaints and clips to, so it has to hold the
    // shadow, and for the raised card the rise as well.
    CardRenderer renderer;
    cardshadow::Cache shadows;
    const Theme theme = Theme::dark();

    CardItem card(&renderer);
    card.setCardHeight(225.0);
    card.setFace(SpadesAce);

    const QRectF body = card.cardRect();

    card.setHeld(false, theme.table, &shadows);

    REQUIRE_FALSE(card.isRaised());
    REQUIRE_FALSE(card.isPlayed());
    REQUIRE(card.look() == cardshadow::Look::Held);
    REQUIRE(card.boundingRect() == body.marginsAdded(cardshadow::reach(cardshadow::Look::Held)));

    card.setHeld(true, theme.table, &shadows);

    REQUIRE(card.isRaised());
    REQUIRE(card.boundingRect()
            == body.translated(0.0, -30.0).marginsAdded(cardshadow::reach(cardshadow::Look::Raised)));
}

TEST_CASE("A card's own rect is its size, centred on its origin", "[hand]")
{
    // WRITTEN OUT, not asked of cardshadow::cardRect(): every other test here, the
    // fan's hit test and the shadows all take "on the card" from that one function,
    // so this is the one place that says what it has to be.
    CardRenderer renderer;

    CardItem card(&renderer);
    card.setCardHeight(225.0);

    const QSizeF size = card.cardSize();

    REQUIRE_FALSE(size.isEmpty());
    REQUIRE(card.cardRect() == QRectF(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height()));

    // And raised, it is still the card at rest: the rise is drawn, not a move.
    cardshadow::Cache shadows;
    card.setHeld(true, Theme::dark().table, &shadows);

    REQUIRE(card.cardRect() == QRectF(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height()));
}

TEST_CASE("Your hand's shadows are pictures of their own in the shared cache", "[hand]")
{
    // One cache for the felt and the hand, as TableScene owns it: the hand's two
    // looks are two more pictures, never a played card's handed back.
    const Theme theme = Theme::dark();
    const cardshadow::Colours colours = cardshadow::colours(theme.table);
    cardshadow::Cache shadows;

    const QSizeF hand(155.0, 225.0);

    shadows.picture(QSizeF(141.0, 205.0), cardshadow::Look::Played, colours, 1.0);
    shadows.picture(hand, cardshadow::Look::Held, colours, 1.0);
    shadows.picture(hand, cardshadow::Look::Raised, colours, 1.0);

    REQUIRE(shadows.renders() == 3);

    // Eight cards at rest share the one.
    for(int i = 0 ; i < 8 ; i++)
        shadows.picture(hand, cardshadow::Look::Held, colours, 1.0);

    REQUIRE(shadows.renders() == 3);
}

// ---- the answer the seated-game tests play with ----
//
// These moved here from PromptStandInTests when C3 deleted it, with the function
// they cover: HandItem took chooseSlot()'s only production caller, so it became
// fixtures::firstLegalSlot(). They are still worth keeping - HumanSeatTests
// answers every card of three whole games with it, and a wrong answer there is a
// re-prompt loop rather than a failure.

TEST_CASE("The tests' own player picks the first LEGAL slot", "[hand]")
{
    REQUIRE(fixtures::firstLegalSlot(promptOf({ false, false, true })) == 2);
}

TEST_CASE("The tests' own player picks slot zero in a blind round", "[hand]")
{
    PlayPrompt prompt;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    REQUIRE(fixtures::firstLegalSlot(prompt) == 0);
}
