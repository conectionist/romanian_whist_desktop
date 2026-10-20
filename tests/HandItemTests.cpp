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

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QMarginsF>
#include <QObject>
#include <QPointF>
#include <QString>

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

    const QSizeF size = card.cardSize();
    const QRectF body(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height());

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
