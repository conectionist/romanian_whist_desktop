#include "CardOrder.h"

#include <algorithm>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

namespace
{

// Display order when there is no trump. Alternating red and black keeps
// neighbouring groups from blurring together.
constexpr Suit SuitOrder[] = { Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds };

} // namespace

namespace cardorder
{

unsigned int suitPosition(Suit suit, const std::optional<Card>& trump)
{
    if(trump && suit == trump->suit)
        return 0;

    for(unsigned int i = 0 ; i < 4 ; i++)
    {
        if(SuitOrder[i] == suit)
            return i + 1;
    }

    return 5;
}

bool inDisplayOrder(const Card& left, const Card& right, const std::optional<Card>& trump)
{
    const unsigned int leftSuit = suitPosition(left.suit, trump);
    const unsigned int rightSuit = suitPosition(right.suit, trump);

    if(leftSuit != rightSuit)
        return leftSuit < rightSuit;

    return static_cast<int>(left.rank) > static_cast<int>(right.rank);
}

void sortForDisplay(std::vector<Card>& cards, const std::optional<Card>& trump)
{
    std::sort(cards.begin(), cards.end(),
        [&trump](const Card& left, const Card& right)
        { return inDisplayOrder(left, right, trump); });
}

} // namespace cardorder
