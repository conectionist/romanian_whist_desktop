#include "Snapshot.h"

#include "HandLayout.h"

#include <cstddef>
#include <optional>

using romanian_whist::GameEngine;

namespace
{
// What the game loop used to accumulate in the client, one push_back per card.
// The engine holds the trick now, and every entry carries the seat that played
// it - so this reads the table off rather than reconstructing it.
void rebuildTable(GameSnapshot& snapshot, const GameEngine& game)
{
    const romanian_whist::Trick& trick = game.getCurrentTrick();
    const std::vector<romanian_whist::PlayedCard>& played = trick.getPlayedCards();

    snapshot.table.clear();

    for(const romanian_whist::PlayedCard& entry : played)
        snapshot.table.push_back({ entry.seat.index, entry.card, false });

    // Who is winning, asked of the engine rather than reimplemented here, so
    // the highlight can never disagree with the winner it eventually declares.
    // It copes with a partly played trick, which is the whole point: this is
    // asked once per card, not once per trick.
    if(const std::optional<romanian_whist::Seat> leading = game.getCurrentTrickLeader())
    {
        for(PlayedCardView& entry : snapshot.table)
            entry.isWinning = entry.seatIndex == leading->index;
    }

    snapshot.hasLeadSuit = trick.hasLeadSuit();

    if(snapshot.hasLeadSuit)
        snapshot.leadSuit = trick.getLeadSuit();

    // The seat the table is walked from. It MUST be whoever led this trick, and
    // getTrickLeaderSeat() is not that from the moment the trick is won - it has
    // already moved on to the winner, which is precisely when the trick-end
    // frame is drawn. The trick's own first entry is the honest answer, and
    // falls back to the leader only while nothing has been played.
    snapshot.leadSeat = played.empty() ? game.getTrickLeaderSeat().index
                                       : played.front().seat.index;
}
}

void refreshFromEngine(GameSnapshot& snapshot, const GameEngine& game)
{
    const auto& players = game.getPlayers();
    const unsigned int playerCount = game.getPlayerCount();

    // getCurrentRoundIndex() is 0-based; roundNumber is 1-based.
    snapshot.roundNumber = game.getCurrentRoundIndex() + 1;
    snapshot.roundCount = game.getRoundCount();
    snapshot.trickCount = game.getCurrentRoundTrickCount();
    snapshot.trickNumber = game.getCurrentTrickNumber();
    snapshot.roundType = game.getCurrentRoundType();
    snapshot.phase = game.getPhase();

    const std::optional<romanian_whist::Card> trump = game.getCurrentTrumpCard();
    snapshot.hasTrump = trump.has_value();
    if(trump)
        snapshot.trump = *trump;

    snapshot.seats.resize(playerCount);
    snapshot.betsPlaced = 0;
    snapshot.betSum = 0;

    for(unsigned int i = 0 ; i < playerCount ; i++)
    {
        const romanian_whist::Player& player = players.at(i);
        SeatView& seat = snapshot.seats[i];

        seat.name = player.getName();
        seat.seatIndex = i;
        seat.isHuman = snapshot.humanSeat >= 0 &&
                       static_cast<unsigned int>(snapshot.humanSeat) == i;

        // Bidding runs from whoever opened the round, wrapping round the table.
        // The engine works that out - reimplementing the modular arithmetic here
        // is exactly the sort of turn-order rule that has no business living in
        // a client.
        seat.bidOrder = game.getBiddingOrder(romanian_whist::Seat{i});

        // Disengaged until this seat has bid, which is what tells a genuine bid
        // of zero from no bid at all.
        const std::optional<unsigned int> bet = game.getBet(romanian_whist::Seat{i});

        seat.hasBet = bet.has_value();
        seat.bet = bet.value_or(0);
        seat.tricksWon = game.getTricksWon(romanian_whist::Seat{i});

        if(seat.hasBet)
        {
            snapshot.betsPlaced++;
            snapshot.betSum += seat.bet;
        }

        seat.cardsLeft = static_cast<unsigned int>(player.getHand().size());
        seat.roundScore = game.getRoundScore(romanian_whist::Seat{i});
        seat.totalScore = game.getTotalScore(romanian_whist::Seat{i});
        seat.winStreak = player.getConsecutiveWins();
        seat.lossStreak = player.getConsecutiveLosses();

        // Rebuilt every frame, same as the human's own hand below: cleared
        // first, then populated only for another seat whose hand the human can
        // currently see (GameEngine::canSeeHand - Forehead rounds, today). Not
        // a spectator's business either way - "a spectator sees everything"
        // stays a client policy nothing here implements, so this only runs once
        // a human is actually seated.
        seat.hand.clear();

        if(snapshot.humanSeat >= 0 && i != static_cast<unsigned int>(snapshot.humanSeat))
        {
            const romanian_whist::Seat viewer{static_cast<unsigned int>(snapshot.humanSeat)};

            if(game.canSeeHand(viewer, romanian_whist::Seat{i}))
                seat.hand = layOutHand(player.getHand(), {}, trump).entries;
        }
    }

    rebuildTable(snapshot, game);

    // Rebuilt every frame rather than leaving whatever was last drawn on
    // screen: cards leave the hand as they are played, and a hand held over
    // from the previous round is worse than none at all.
    //
    // Laid out with no legal set, which is what says "nothing is being chosen
    // here". The move provider lays it out again, with real legality, when it
    // is actually this seat's turn to choose a card.
    snapshot.hand.clear();

    if(snapshot.humanSeat < 0)
        return;

    const romanian_whist::Seat humanSeat{static_cast<unsigned int>(snapshot.humanSeat)};

    // Forehead: the human cannot see their own hand while it is being bid on.
    // Hidden: nobody can. Both leave this cleared, same as spectator games.
    if(!game.canSeeHand(humanSeat, humanSeat))
        return;

    snapshot.hand = layOutHand(players.at(static_cast<std::size_t>(snapshot.humanSeat)).getHand(),
                               {},
                               trump).entries;
}
