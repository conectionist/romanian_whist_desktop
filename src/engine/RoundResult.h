#ifndef ROUND_RESULT_H
#define ROUND_RESULT_H

// What a round was worth, captured at the one callback where it can be read.
//
// A queued signal payload, so plain copyable data with nothing pointing back
// into the engine - the same rules Prompts.h states for the two prompts, and the
// same reason for its own header: GameBridge declares the signal, MainWindow
// connects to it, and a widget that wants the type should not have to include
// the bridge to get it.
//
// WHY THIS EXISTS AT ALL, given that the snapshot published immediately before
// roundScored() already carries most of these numbers. Two things it cannot:
//
//   * nextRoundCountsForStreaks, below - a fact about the NEXT round, readable
//     only from the engine and therefore only on the game thread (section 3);
//   * a HISTORY. The engine stores no per-round score, and onRoundScored() is
//     the only callback at which one can be read against the right round index
//     (section 21.3). MainWindow keeps every one for the Round history sheet -
//     see the note at the foot of this header.
//
// AND ONE REASON THAT DOES NOT HOLD, written down so it is not re-derived: that
// a snapshot-driven sheet would be wiped by onRoundComplete()'s next publish. It
// would not. The game thread is inside pace() for the whole time the sheet is up
// - dwelling, or held for a player who asked to be waited for - so nothing else
// is published until it is dismissed. This is NOT C3's HandItem hazard.

#include <romanian_whist/Card.h>
#include <romanian_whist/RoundType.h>

#include <QMetaType>

#include <string>
#include <vector>

struct RoundSeatResult
{
    std::string name;
    unsigned int seatIndex = 0;
    bool isHuman = false;

    // A round that was stopped mid-bidding has seats with no bet. Reaching the
    // sheet at all means the round was scored, so this is true in practice -
    // carried anyway because SeatView carries it and the two are read together.
    bool hasBet = false;
    unsigned int bet = 0;
    unsigned int tricksWon = 0;

    // THE TWO HALVES OF SECTION 16.1's "34 -> 41", and they are not the same
    // number at different times: IGameObserver.h says outright that inside
    // onRoundScored() getRoundScore() is what THIS round was worth and
    // getTotalScore() is the committed total it has NOT been folded into yet.
    //
    // So the arrow is totalScore -> totalScore + roundScore. A client showing
    // one without the other reads differently either side of the commit; the
    // arrow shows both and needs no explanation.
    int roundScore = 0;
    int totalScore = 0;

    // ALREADY INCLUDING THIS ROUND. Scoreboard::calculateScores() updates the
    // streaks before onRoundScored() fires, so a four here means the NEXT one is
    // the fifth - which is what section 16.1's warning line says.
    unsigned int winStreak = 0;
    unsigned int lossStreak = 0;
};

struct RoundResult
{
    unsigned int roundNumber = 0;      // 1-based, the way GameSnapshot counts
    unsigned int roundCount = 0;
    unsigned int trickCount = 0;       // cards dealt this round

    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;

    bool hasTrump = false;             // no trump in an 8-card round
    romanian_whist::Card trump{};

    // THE ONE FIELD NO SNAPSHOT COULD CARRY, and the sharpest reason this type
    // exists. The engine awards +/-10 on the fifth consecutive exact bid or
    // miss, and ONE-CARD ROUNDS COUNT FOR NEITHER, in both directions - they
    // neither advance a streak nor break one (docs/RULES.md, and
    // Scoreboard::shouldCountForStreaks).
    //
    // So "a fifth costs 10" is WRONG when the next round deals one card, and
    // getRound(index + 1).getTrickCount() is the only way to know. The schedule
    // is laid out at start(), so that round exists long before it is played -
    // but only the game thread may ask.
    //
    // False on the final round, where there is no next round to count.
    bool nextRoundCountsForStreaks = false;

    // WHETHER THE BRIDGE HELD THE GAME FOR THIS RESULT, until the window's
    // Continue: the round wait as the game thread read it, once, for this round.
    // The window reads the hold from here rather than from a copy of its own,
    // which Settings could change between the two (PR #35's review, finding #6).
    bool held = false;

    // In seat order, which is the order the scoreboard and the sheet both draw.
    std::vector<RoundSeatResult> seats;
};

// MAINWINDOW ACCUMULATES THESE, since the table redesign's phase 11: one per
// round, appended from the queued signal and read by the Round history sheet. Not
// section 16.3's sketch of a history owned by GameBridge, which lives on the GAME
// thread and would write it from there; a sheet reading that would be the GUI
// thread reading engine-thread state, which is the one rule section 3 has. The
// window's copy is the same data with no boundary crossed.

// Beside the struct rather than in GameBridge.h, for the reason Prompts.h gives
// at length: a translation unit that includes only this header must see the same
// specialization the rest of the program does.
Q_DECLARE_METATYPE(RoundResult)

#endif
