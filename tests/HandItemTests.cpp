#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameFixtures.h"

#include "paint/CardRenderer.h"
#include "table/CardItem.h"
#include "table/HandItem.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QObject>
#include <QPointF>

#include <cstddef>
#include <vector>

using namespace romanian_whist;

// The fan, and the two things about it that are not obvious.
//
// THE FIRST IS THE HIT TEST. The cards overlap by 40% of their width, so most of
// the fan is covered by its neighbour - and the card a player is pointing at is
// the LAST one whose box contains the cursor, not the first. Walking forwards
// returns the card underneath, and every click in an overlap plays a neighbour.
// That is the same defect PlayPrompt::handIndex exists to prevent, arriving from
// the geometry instead of from the indexing.
//
// THE SECOND IS THAT THE FAN HAS TWO SOURCES. Between turns it is the snapshot's
// hand and is never playable; on your turn it is the prompt, with legality that
// means something - or, in a blind round, a count of face-down backs and nothing
// else (sections 4.0 and 17).

namespace
{
constexpr qreal kCardHeight = 100.0;

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
}

TEST_CASE("The fan draws one card per slot and centres them", "[hand]")
{
    Fanned fan(promptOf({ true, true, true, true, true }));

    REQUIRE(fan.item.slotCount() == 5);
    REQUIRE(fan.cards().size() == 5);

    // CENTRED ON THE ITEM'S ORIGIN, which TableScene puts at the middle of the
    // strip it reserved. Section 14: "the fan is centred".
    const QRectF box = fan.item.boundingRect();

    REQUIRE(box.left() < 0.0);
    REQUIRE(box.right() > 0.0);
    REQUIRE(std::abs(box.left() + box.right()) < 1.0);
}

TEST_CASE("The cards overlap rather than sitting end to end", "[hand]")
{
    // Section 14: "cards overlap by about 40% of their width". The assertion is
    // the RELATION and not the fraction - eight cards laid end to end are wider
    // than the fan is - because the fraction is a constant the design may move
    // and the overlap is the property that must not go away. Without it a
    // full hand does not fit the strip at all.
    CardRenderer renderer;

    const qreal cardWidth = renderer.sizeForHeight(kCardHeight).width();

    Fanned fan(promptOf({ true, true, true, true, true, true, true, true }));

    REQUIRE(fan.item.boundingRect().width() < cardWidth * 8.0);
    REQUIRE(fan.item.boundingRect().width() > cardWidth);
}

TEST_CASE("The slot under an overlap is the upper card, not the lower", "[hand]")
{
    // THE CASE THIS FILE EXISTS FOR. Card i+1 covers the right 40% of card i, so
    // a point inside that strip is inside BOTH boxes - and the one the player can
    // see, and therefore the one they mean, is i+1. A forward walk returns i.
    CardRenderer renderer;
    const qreal cardWidth = renderer.sizeForHeight(kCardHeight).width();

    Fanned fan(promptOf({ true, true, true }));

    const std::vector<CardItem*> cards = fan.cards();

    REQUIRE(cards.size() == 3);

    // Just inside the left edge of the middle card, which is still over the
    // first one.
    const QPointF where(cards[1]->pos().x() - cardWidth / 2.0 + 2.0, 0.0);

    // THE PREMISE, asserted rather than assumed: this point really is inside both
    // boxes. Without that the case would pass on a fan that did not overlap at
    // all and would be saying nothing.
    REQUIRE(cards[0]->boundingRect().translated(cards[0]->pos()).contains(where));
    REQUIRE(cards[1]->boundingRect().translated(cards[1]->pos()).contains(where));

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
