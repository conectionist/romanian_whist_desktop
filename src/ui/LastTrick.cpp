#include "LastTrick.h"

#include <utility>

void lasttrick::Tracker::observe(const GameSnapshot& snapshot)
{
    // ONLY A FINISHED TRICK: a card from every seat. Partly played tables, the
    // next trick's included, leave the kept one as it is.
    if(snapshot.seats.empty() || snapshot.table.size() != snapshot.seats.size())
        return;

    Trick trick;
    trick.roundNumber = snapshot.roundNumber;
    trick.humanSeat = snapshot.humanSeat;

    for(const PlayedCardView& view : snapshot.table)
    {
        Played played;
        played.card = view.card;
        played.seatIndex = view.seatIndex;
        played.isWinning = view.isWinning;

        if(view.seatIndex < snapshot.seats.size())
            played.name = QString::fromStdString(snapshot.seats[view.seatIndex].name);

        trick.cards.push_back(played);
    }

    kept = std::move(trick);
}
