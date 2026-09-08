#include "PromptStandIn.h"

namespace promptstandin
{

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
