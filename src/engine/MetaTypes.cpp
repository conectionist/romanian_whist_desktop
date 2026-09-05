#include "MetaTypes.h"

#include "GameBridge.h"
#include "Prompts.h"

#include "core/GameSnapshot.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/GameEngine.h>

#include <memory>
#include <vector>

void registerMetaTypes()
{
    // The snapshot, which is what twelve of the thirteen callbacks publish.
    qRegisterMetaType<std::shared_ptr<const GameSnapshot>>();

    // The two prompts. Only phase C connects anything to them, but registering
    // is done in one place or it is done inconsistently.
    qRegisterMetaType<BetPrompt>();
    qRegisterMetaType<PlayPrompt>();

    qRegisterMetaType<romanian_whist::Card>();

    // Easy to miss, because it is the only payload that is not one of this
    // client's own types: gameOver() carries TWO of these across the boundary.
    qRegisterMetaType<std::vector<romanian_whist::Standing>>();
}
