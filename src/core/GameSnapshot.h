#ifndef GAME_SNAPSHOT_H
#define GAME_SNAPSHOT_H

// Ported from romanian_whist_terminal, src/GameView.h - struct GameView and the
// three types under it, minus the fields a GUI has no use for: `message`,
// `hint`, `error`, `elapsedMinutes`, `Phase`, and HandEntry::choice, which is
// the number a terminal player types. The terminal's own Phase went; the
// engine's GamePhase is carried below, so nothing here guesses it.
//
// What the screen shows, as plain data. Widgets read only this, never the
// engine - see QT_CLIENT_PLAN_proposal1.md section 3. The engine has no locking
// anywhere, so a GUI-thread read racing the game thread is a torn read of a
// hand mid-mutation.
//
// EVERY MEMBER IS COPYABLE AND DEFAULT-CONSTRUCTIBLE, and must stay that way:
// this crosses a thread boundary as a queued signal payload. No references, no
// unique_ptr, and no bare Seat - Seat has no default constructor.

#include <romanian_whist/Card.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/RoundType.h>

#include "PlayerProfile.h"

#include <string>
#include <vector>

struct HandEntry
{
    romanian_whist::Card card{};

    // Whether this card can be played on the current trick; false greys it out.
    //
    // ON A SNAPSHOT THIS IS ALWAYS TRUE. The snapshot's hand is laid out with an
    // empty legal set, which is what says "nothing is being chosen here", so
    // nothing can be greyed out from a snapshot at all. Legality that means
    // something arrives on PlayPrompt - see src/engine/QtMoveProvider.h.
    bool legal = true;
};

struct SeatView
{
    std::string name;
    unsigned int seatIndex = 0;

    // FROM THE SEAT'S PlayerProfile, not the engine, which knows only the name:
    // GameBridge stamps it on from the deal's profiles. Male for a seat that came
    // without one - every fixture that builds a GameSetup by hand.
    Gender gender = Gender::Male;

    // The portrait this seat wears, an avatar id (PlayerProfile::avatar), stamped
    // on by GameBridge as the gender is. Empty without a profile: the bare box.
    std::string avatar;

    // Position in this round's bidding, 1 for whoever opened it.
    unsigned int bidOrder = 0;

    bool isHuman = false;

    // Tells a genuine bid of 0 from no bid at all.
    bool hasBet = false;
    unsigned int bet = 0;
    unsigned int tricksWon = 0;
    unsigned int cardsLeft = 0;
    int roundScore = 0;
    int totalScore = 0;
    unsigned int winStreak = 0;
    unsigned int lossStreak = 0;

    // This seat's hand in display order, filled only for a seat that is NOT the
    // human's and only where canSeeHand() allows - a Forehead round, today.
    std::vector<HandEntry> hand;
};

struct PlayedCardView
{
    unsigned int seatIndex = 0;
    romanian_whist::Card card{};

    // The engine's ranking, never recomputed - so the highlight cannot disagree
    // with the winner the engine eventually declares.
    bool isWinning = false;
};

struct GameSnapshot
{
    unsigned int roundNumber = 0;      // 1-based; the engine's index is 0-based
    unsigned int roundCount = 0;
    unsigned int trickCount = 0;
    unsigned int trickNumber = 0;      // 0 between the deal and the first trick
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;

    // GameEngine::getPhase(), AS IT IS - not worked out from the bets and the seat
    // on turn. Playing from onBettingComplete on, before anyone is asked for a
    // card; RoundScored or GameOver once the last trick is won. The lead rhombus
    // is drawn in Playing only, and the last bidder is still on turn in Betting.
    romanian_whist::GamePhase phase = romanian_whist::GamePhase::NotStarted;

    bool hasTrump = false;             // no trump in an 8-card round
    romanian_whist::Card trump{};

    std::vector<SeatView> seats;

    // -1 is "nobody", which is why these are int rather than Seat: a spectator
    // game has no human seat, nobody is active between turns, and Seat has
    // neither a default constructor nor a sentinel to say either.
    int humanSeat = -1;
    int activeSeat = -1;
    unsigned int leadSeat = 0;         // who LED this trick, not who leads next

    bool hasLeadSuit = false;
    romanian_whist::Suit leadSuit{};
    std::vector<PlayedCardView> table;

    // The human's own hand, display order. FOR LOOKING AT - it carries no
    // legality (see HandEntry above) and it is EMPTY in a Forehead or Hidden
    // round, because canSeeHand(human, human) is false in both. Since
    // endWithForeheadAndHidden defaults to true, every default game ends with
    // two rounds in which this is empty while the engine waits for a card.
    //
    // So this is never what a player clicks. That comes from PlayPrompt.
    std::vector<HandEntry> hand;

    unsigned int betsPlaced = 0;
    unsigned int betSum = 0;

    // Whether `seat` is the human's: never in a spectator game. The one place this
    // is worked out from humanSeat (PR #40's review, finding #8).
    bool isHumanSeat(unsigned int seat) const
    {
        return humanSeat >= 0 && static_cast<unsigned int>(humanSeat) == seat;
    }
};

#endif
