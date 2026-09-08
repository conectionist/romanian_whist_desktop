#include "PromptStandIn.h"

namespace promptstandin
{

unsigned int chooseBet(const BetPrompt& prompt)
{
    // The two rules GameEngine::isBetLegal() is made of, restated the way
    // QtMoveProvider::makeBet() restates them: no more tricks than there are
    // cards, and never the one bet that would make the totals add up.
    //
    // Walked rather than written as `forbidden == 0 ? 1 : 0`, which is the same
    // answer everywhere it is defined and is undefined in the one place it is
    // not: a 0-card round would have that return a bid of 1 against a hand of
    // none. No schedule deals one today - the shortest round is one card - so
    // this is a loop over at most two iterations to avoid encoding a fact about
    // the schedule in a client that has no business holding one.
    for(std::size_t bet = 0 ; bet <= prompt.handSize ; bet++)
    {
        const unsigned int candidate = static_cast<unsigned int>(bet);

        if(!prompt.forbiddenBet || candidate != *prompt.forbiddenBet)
            return candidate;
    }

    // Unreachable: forbiddenBet is one value and the range holds at least one
    // more (handSize is at least 1 in every round the engine schedules). If it
    // ever is reached, makeBet() re-prompts rather than handing this to the
    // engine, so the failure is a loop in the log and not a dead game.
    return 0;
}

std::size_t chooseSlot(const PlayPrompt& prompt)
{
    // FIRST LEGAL, not first. The prompt marks each slot legal or not and the
    // widget is meant to refuse the rest; a stand-in that ignored the flag would
    // be answered by playCard()'s own guard with a re-prompt, and the resulting
    // loop is exactly the defect this is standing in to rule out.
    for(std::size_t slot = 0 ; slot < prompt.entries.size() ; slot++)
    {
        if(prompt.entries[slot].legal)
            return slot;
    }

    // Two ways to arrive here, and both mean slot 0.
    //
    // A BLIND ROUND, which is the ordinary one: `entries` is empty because the
    // player is not entitled to see the card, `handIndex` still carries the way
    // back, and there is one card to play regardless (section 17).
    //
    // And a hand with entries but nothing legal, which cannot happen -
    // QtMoveProvider::playCard() returns nullopt without raising a prompt at all
    // when the legal set is empty - so this is the arm that keeps an impossible
    // state from being an out-of-range return rather than a case being handled.
    return 0;
}

} // namespace promptstandin
