#include <catch2/catch_test_macros.hpp>

#include "table/TrickItem.h"
#include "table/CardItem.h"
#include "core/GameSnapshot.h"
#include "paint/Theme.h"

#include <QList>
#include <QPointF>

#include <vector>

// The defensive branch, asserted rather than assumed.
//
// setTrick() skips a table entry whose seatIndex the ring does not have, which
// is the right call - a seat this scene cannot place is a card it cannot draw,
// and indexing past the end of the vector is the alternative. What the skip has
// to ALSO do is hide the item at that index, because the hide pass that cleans
// up after a shrinking trick starts past the end of the table and never reaches
// an index inside it.
//
// The engine never produces such a snapshot today, which is exactly why this
// file exists: an unreachable branch is one nothing corrects when it stops being
// unreachable, and its failure mode here is silent - last frame's card left on
// the table under a seat that no longer has one.
namespace
{

// A four-seat ring, spaced far enough apart that nothing here depends on the
// geometry. The cards are placed at these points; only which of them are
// VISIBLE is ever asserted.
const std::vector<QPointF> ring{ QPointF(100.0, 200.0),
                                 QPointF(0.0, 100.0),
                                 QPointF(100.0, 0.0),
                                 QPointF(200.0, 100.0) };

const QPointF centre(100.0, 100.0);

constexpr qreal cardHeight = 60.0;

GameSnapshot trickOf(std::vector<PlayedCardView> table)
{
    GameSnapshot snapshot;
    snapshot.table = std::move(table);
    return snapshot;
}

PlayedCardView played(unsigned int seat, romanian_whist::Rank rank)
{
    PlayedCardView entry;
    entry.seatIndex = seat;
    entry.card = romanian_whist::Card(rank, romanian_whist::Suit::Hearts);
    return entry;
}

// The cards in creation order, which is the order cardAt() grows them and so is
// the trick-entry order the assertions are written in.
std::vector<CardItem*> cardsOf(const TrickItem& trick)
{
    std::vector<CardItem*> cards;

    for(QGraphicsItem* child : trick.childItems())
    {
        if(CardItem* card = dynamic_cast<CardItem*>(child))
            cards.push_back(card);
    }

    return cards;
}

}

TEST_CASE("A trick entry the ring cannot place hides its card rather than stranding it", "[trick]")
{
    // Null renderer: nothing here paints, and CardItem's paint() and
    // sizeForHeight() both guard against one. Building a real CardRenderer would
    // only add the card art to a test about visibility.
    TrickItem trick(nullptr);
    const Theme theme = Theme::light();

    // Frame one: two cards, both placeable, both on the table.
    trick.setTrick(trickOf({ played(0, romanian_whist::Rank::Ace),
                             played(1, romanian_whist::Rank::King) }),
                   ring, centre, cardHeight, theme);

    std::vector<CardItem*> cards = cardsOf(trick);

    REQUIRE(cards.size() == 2);
    REQUIRE(cards[0]->isVisible());
    REQUIRE(cards[1]->isVisible());

    // Frame two: the same two entries, but the first names a seat this ring does
    // not have. THE TABLE HAS NOT SHRUNK - it is still two entries long - so the
    // hide pass below the loop starts at index 2 and never touches index 0.
    trick.setTrick(trickOf({ played(99, romanian_whist::Rank::Queen),
                             played(1, romanian_whist::Rank::King) }),
                   ring, centre, cardHeight, theme);

    cards = cardsOf(trick);

    REQUIRE(cards.size() == 2);

    // The whole finding: without the hide, this is still showing frame one's ace
    // at seat 0, with its tilt and its winning highlight.
    REQUIRE_FALSE(cards[0]->isVisible());

    // And the entry that IS placeable is untouched by its neighbour's skip.
    REQUIRE(cards[1]->isVisible());
}

// The pip marks the lead, and the lead is snapshot.table.front() - the same
// entry the case above puts out of range. Its guard is separate from the card
// loop's, so a fix to one that forgets the other leaves a pip floating over a
// seat that was never drawn.
TEST_CASE("An unplaceable lead draws no pip", "[trick]")
{
    TrickItem trick(nullptr);
    const Theme theme = Theme::light();

    trick.setTrick(trickOf({ played(0, romanian_whist::Rank::Ace) }),
                   ring, centre, cardHeight, theme);

    REQUIRE_FALSE(trick.boundingRect().isEmpty());

    trick.setTrick(trickOf({ played(99, romanian_whist::Rank::Ace) }),
                   ring, centre, cardHeight, theme);

    REQUIRE(trick.boundingRect().isEmpty());
}
