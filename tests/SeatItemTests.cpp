#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "paint/CardRenderer.h"
#include "paint/Theme.h"
#include "table/CardItem.h"
#include "table/SeatItem.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QSizeF>
#include <QString>

#include <vector>

using namespace romanian_whist;

// The plaque, which until C4 was the one item on the table with no test file at
// all - and B2b's four findings were every one of them in a painted widget of
// this shape.
//
// WHAT C4 ADDED TO IT is section 17's other half: in a Forehead round every seat
// but your own shows the card it is holding, face up, so you can bid on what
// everyone else has. The card takes the slot the cards-left stack occupies, and
// the two things that must hold are that the PLAQUE DOES NOT CHANGE SIZE - it
// feeds the layout, so a plaque that grew for two rounds would move what is laid
// out against it twice a game - and that the TEXT IS MEASURED at the width it is
// drawn into.

namespace
{
constexpr qreal kCardHeight = 120.0;

// What a seat that can be seen looks like: canSeeHand() allowed it, so the
// snapshot carries the card. Nothing here asks "is it a Forehead round" and the
// item does not either - a non-empty hand IS the permission.
SeatView seen(const QString& name, Card card)
{
    SeatView view;

    view.name = name.toStdString();
    view.seatIndex = 1;
    view.cardsLeft = 1;
    view.hand.push_back(HandEntry{ card, true });

    return view;
}

SeatView unseen(const QString& name, unsigned int cardsLeft = 5)
{
    SeatView view;

    view.name = name.toStdString();
    view.seatIndex = 1;
    view.cardsLeft = cardsLeft;

    return view;
}

QSizeF faceFor(qreal cardHeight)
{
    CardRenderer renderer;

    return renderer.sizeForHeight(seatitem::faceHeightFor(cardHeight));
}

// A plaque in a scene, so its child items can be asked about. Declaration order
// is load-bearing the same way HandItemTests' is: the renderer outlives the item
// and the item leaves the scene before the scene goes.
struct Plaque
{
    QGraphicsScene scene;
    CardRenderer renderer;
    SeatItem item{ &renderer };

    explicit Plaque(const SeatView& view)
    {
        item.setCardHeight(kCardHeight);
        item.setSeat(view, false, Theme::light());

        scene.addItem(&item);
    }

    // The face-up card, if one is showing. Found by type rather than tracked, so
    // the test asks the item what it built.
    CardItem* face() const
    {
        for(QGraphicsItem* child : item.childItems())
        {
            if(auto* card = dynamic_cast<CardItem*>(child))
            {
                if(card->isVisible())
                    return card;
            }
        }

        return nullptr;
    }
};
}

TEST_CASE("The plaque is the same size whether or not a card is shown", "[seat]")
{
    // THE GUARANTEE THE WHOLE PLACEMENT DECISION EXISTS TO KEEP. plaqueSize()
    // feeds the layout - the bid strip's band today, and until the table
    // redesign's phase 2 every seat and every played card - so a plaque that grew
    // for the two blind rounds would move them twice a game, which is the defect
    // B2b recorded against the info strip, one layer out.
    //
    // It is a static taking only the card height, so the only way it could depend
    // on the hand is if someone made it stop being one. This is the case that
    // would notice.
    const QSizeF withCard = SeatItem::plaqueSize(kCardHeight);

    Plaque showing(seen(QStringLiteral("Ana"), Card(Rank::King, Suit::Spades)));
    Plaque hiding(unseen(QStringLiteral("Ana")));

    REQUIRE(showing.face() != nullptr);
    REQUIRE(hiding.face() == nullptr);

    REQUIRE(SeatItem::plaqueSize(kCardHeight) == withCard);
    REQUIRE(showing.item.boundingRect() == hiding.item.boundingRect());
}

TEST_CASE("A seat whose hand can be seen shows it, and one whose cannot does not", "[seat]")
{
    // BOTH DIRECTIONS, so a plaque that always drew a card fails too. The empty
    // case is the ordinary one - twenty-four rounds of twenty-six, plus every
    // seat in a Hidden round, plus your own seat in a Forehead one.
    Plaque showing(seen(QStringLiteral("Bogdan"), Card(Rank::Ace, Suit::Hearts)));

    REQUIRE(showing.face() != nullptr);

    Plaque hiding(unseen(QStringLiteral("Bogdan")));

    REQUIRE(hiding.face() == nullptr);
}

TEST_CASE("Your own seat shows no face in a Forehead round", "[seat]")
{
    // THE CASE THAT WOULD LEAK YOUR OWN CARD TO YOU. canSeeHand(human, human) is
    // false in Forehead, so refreshFromEngine() leaves your own seat's hand empty
    // - and the plaque has to fall back to the stack rather than inventing
    // something to draw. Section 17: your card is face-down in your own fan and
    // nowhere else.
    SeatView mine = unseen(QStringLiteral("You"), 1);
    mine.isHuman = true;

    REQUIRE(mine.hand.empty());

    Plaque plaque(mine);

    REQUIRE(plaque.face() == nullptr);
}

TEST_CASE("The face-up card stays inside the plaque", "[seat]")
{
    // At several sizes including Theme's floor, because the card is a fraction of
    // the PLAQUE's height rather than of the card unit - the plaque's padded inner
    // box is only 0.38 card-heights and nothing legible fits in it, so the card
    // uses the plaque's full height less a margin and this is what holds it there.
    for(const qreal height : { 44.0, 80.0, kCardHeight, 220.0 })
    {
        INFO("card height " << height);

        const seatitem::Layout layout = seatitem::layoutFor(height, faceFor(height));

        REQUIRE_FALSE(layout.card.isEmpty());
        REQUIRE(layout.plaque.contains(layout.card));
    }
}

TEST_CASE("The text column gives up the width the card takes", "[seat]")
{
    // B2b'S FINDING, one plaque further on: a widget measuring itself with
    // something other than what it later draws with. The name is elided against
    // this rect, so a text width that did not narrow here would be measured at
    // the full plaque and drawn beside a card - and a long name would run
    // underneath it.
    const seatitem::Layout without = seatitem::layoutFor(kCardHeight, QSizeF());
    const seatitem::Layout with = seatitem::layoutFor(kCardHeight, faceFor(kCardHeight));

    REQUIRE(with.text.width() < without.text.width());

    // By at least the card, which is what says the narrowing is the card's doing
    // rather than an unrelated margin.
    REQUIRE(without.text.width() - with.text.width() >= with.card.width());

    // AND THEY DO NOT OVERLAP. The two rects are the whole point of computing
    // them together.
    REQUIRE_FALSE(with.text.intersects(with.card));
}

TEST_CASE("A long name is elided rather than run under the card", "[seat]")
{
    // The consequence of the case above, asserted the way the plaque actually
    // asks it. A name from section 13's dialog is arbitrary text.
    const seatitem::Layout with = seatitem::layoutFor(kCardHeight, faceFor(kCardHeight));

    QFont font;
    font.setPointSizeF(std::max(6.0, kCardHeight * 0.56 * 0.26));
    font.setBold(true);

    const QFontMetricsF metrics(font);
    const QString name = QStringLiteral("Bartholomew Featherstonehaugh");

    const QString elided = metrics.elidedText(name, Qt::ElideRight, with.text.width());

    REQUIRE(metrics.horizontalAdvance(elided) <= with.text.width());
    REQUIRE(elided != name);
}

TEST_CASE("With no card the layout is what it was before C4", "[seat]")
{
    // The stack's slot is the inner box's right edge whether a card is drawn
    // there or not, so the twenty-four ordinary rounds are untouched.
    const seatitem::Layout layout = seatitem::layoutFor(kCardHeight, QSizeF());

    REQUIRE(layout.card.isEmpty());
    REQUIRE(layout.text == layout.stack);
}

TEST_CASE("A seat with nothing to show lays out as though nothing were there", "[seat]")
{
    // THE OTHER CONSEQUENCE of believing you have a card, and the one the card
    // itself does not reveal: refreshFace() checks the hand again before it
    // dereferences, so a seat that wrongly thought it had one would still draw no
    // card - and would still narrow its text column and drop its cards-left
    // stack. Twenty-four rounds of twenty-six look wrong and nothing crashes.
    Plaque hiding(unseen(QStringLiteral("Cristina")));

    REQUIRE_FALSE(hiding.item.showsFace());

    const seatitem::Layout layout = hiding.item.layoutFor();

    REQUIRE(layout.card.isEmpty());

    // The full inner box, exactly as before C4 - which is what says the stack is
    // still drawn and the name still has the whole plaque to elide against.
    REQUIRE(layout.text == layout.stack);
}

TEST_CASE("A seat with a card to show narrows its own text column", "[seat]")
{
    // The same question from the other side, asked of the ITEM rather than of the
    // free function - so the plaque's own decision is under test and not just the
    // arithmetic it delegates to.
    Plaque showing(seen(QStringLiteral("Dan"), Card(Rank::Ten, Suit::Clubs)));

    REQUIRE(showing.item.showsFace());

    const seatitem::Layout layout = showing.item.layoutFor();

    REQUIRE_FALSE(layout.card.isEmpty());
    REQUIRE(layout.text.width() < layout.stack.width());
}
