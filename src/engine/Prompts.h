#ifndef PROMPTS_H
#define PROMPTS_H

// What a parked QtMoveProvider asks the GUI, and everything an answer needs.
//
// Between them these carry the whole question, so nothing in a click handler
// has to go looking elsewhere - and nothing in either points back into the
// engine. They are queued signal payloads, so both are plain copyable data.
//
// They live in their own header rather than in QtMoveProvider.h because
// GameBridge declares signals carrying them and the provider raises those
// signals through the bridge: putting them here is what keeps the two headers
// from having to include each other.

#include "core/GameSnapshot.h"

#include <romanian_whist/RoundType.h>

#include <QMetaType>

#include <cstddef>
#include <optional>
#include <vector>

struct BetPrompt
{
    // The generation this answers. A submission naming any other generation is
    // dropped - see QtMoveProvider::submit().
    unsigned int promptId = 0;

    // The round's trick count, and so the highest legal bid. BetContext carries
    // no handSize of its own; the hand's size IS that number.
    std::size_t handSize = 0;

    std::optional<unsigned int> forbiddenBet;
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;
    bool isFirstPlayer = false;

    // EMPTY IN A FOREHEAD OR HIDDEN ROUND. The engine passes the real hand
    // because it has no other one to give; a prompt that never carries it
    // cannot leak it through a tooltip, a debug overlay or a later refactor.
    //
    // Otherwise the whole hand in display order, laid out with no legal set -
    // nothing is being chosen on a bidding screen, so nothing is greyed out.
    std::vector<HandEntry> hand;
};

struct PlayPrompt
{
    unsigned int promptId = 0;

    // The whole hand in DISPLAY order, with `legal` finally meaning something:
    // this is the one place in the program where a legal set exists at all. A
    // snapshot's hand cannot stand in for it - see core/GameSnapshot.h.
    //
    // Empty in a blind round, on the same rule as BetPrompt::hand.
    std::vector<HandEntry> entries;

    // handIndex[i] is the position in the ENGINE's hand of display slot i, so a
    // click handler has the way back without consulting a snapshot, a cache, or
    // anything else that could be a turn out of date.
    //
    // CARRIED EVEN IN A BLIND ROUND, where `entries` is not: a position names no
    // card, so it leaks nothing, and it keeps the click handler free of a
    // special case. Its size is also how many face-down backs to draw.
    std::vector<std::size_t> handIndex;

    // Forehead or Hidden. Both are 1-trick rounds, so the hand is one card and
    // the play is forced - and the player is not entitled to see it.
    bool blind = false;
};

// HERE, not in GameBridge.h where the signals carrying these are declared. Qt 6
// builds a QMetaType from any complete type, so a queued connection usually
// resolves them with the macro absent and omitting it is not a compile error;
// wherever auto-registration does not reach, the failure is a silent "cannot
// queue argument of type X" on stderr and a signal that never arrives.
//
// Declaring them beside the structs is what makes that guarantee hold for every
// translation unit. Left in GameBridge.h, a click handler that included only
// this header - which is the whole reason this header exists - would instantiate
// the primary QMetaTypeId template while the rest of the program used the
// specialization: an ODR violation whose visible symptom is exactly the dropped
// signal the macro is here to prevent.
Q_DECLARE_METATYPE(BetPrompt)
Q_DECLARE_METATYPE(PlayPrompt)

#endif
