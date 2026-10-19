#ifndef TABLE_FIXTURES_H
#define TABLE_FIXTURES_H

#include "core/GameSnapshot.h"

#include <romanian_whist/Card.h>

#include <string>

// A FULL TABLE, shared by what measures a resize and what asserts on one:
// tests/ResizeTiming.cpp times a drag over it, and TableViewTests counts what the
// same drag rasterizes. One fixture, so the times describe the case the tests hold.
//
// Six plaques, eight cards in your hand and five on the felt, court cards among
// both: the court cards' art is the slow kind to rasterize. The Ace of Spades is
// in your hand, which is what a test probes the card cache with.
//
// MID-TRICK, so everything a trick draws is drawn: every bid in and the engine in
// its play phase, so the lead rhombus is on the felt; and the King of Spades
// winning, so the largest shadow picture - the winner's deeper shadow, glow and
// ring - is among those a drag has to leave alone.
inline GameSnapshot fullTable()
{
    using romanian_whist::Card;
    using romanian_whist::Rank;
    using romanian_whist::Suit;

    GameSnapshot snapshot;

    snapshot.humanSeat = 0;
    snapshot.roundNumber = 10;
    snapshot.roundCount = 32;
    snapshot.trickCount = 8;
    snapshot.trickNumber = 3;
    snapshot.phase = romanian_whist::GamePhase::Playing;
    snapshot.leadSeat = 1;
    snapshot.seats.resize(6);

    for(unsigned int i = 0 ; i < 6 ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].name = "Seat " + std::to_string(i);
        snapshot.seats[i].cardsLeft = 8;
        snapshot.seats[i].hasBet = true;
        snapshot.seats[i].bet = 1;
    }

    snapshot.betsPlaced = 6;
    snapshot.betSum = 6;

    snapshot.seats[0].isHuman = true;

    snapshot.hand = { HandEntry{ Card(Rank::Two, Suit::Clubs) },
                      HandEntry{ Card(Rank::Seven, Suit::Clubs) },
                      HandEntry{ Card(Rank::Jack, Suit::Clubs) },
                      HandEntry{ Card(Rank::Nine, Suit::Diamonds) },
                      HandEntry{ Card(Rank::Queen, Suit::Diamonds) },
                      HandEntry{ Card(Rank::Ten, Suit::Hearts) },
                      HandEntry{ Card(Rank::King, Suit::Hearts) },
                      HandEntry{ Card(Rank::Ace, Suit::Spades) } };

    snapshot.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Spades), true },
                       PlayedCardView{ 2, Card(Rank::Queen, Suit::Spades) },
                       PlayedCardView{ 3, Card(Rank::Jack, Suit::Spades) },
                       PlayedCardView{ 4, Card(Rank::Eight, Suit::Spades) },
                       PlayedCardView{ 5, Card(Rank::Three, Suit::Spades) } };

    return snapshot;
}

#endif
