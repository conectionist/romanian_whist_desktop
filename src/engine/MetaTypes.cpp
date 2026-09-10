#include "MetaTypes.h"

#include "GameBridge.h"
#include "Prompts.h"
#include "RoundResult.h"

#include "core/GameSnapshot.h"

#include <romanian_whist/Card.h>
#include <romanian_whist/GameEngine.h>

#include <memory>
#include <vector>

void registerMetaTypes()
{
    // The snapshot, which is what twelve of the thirteen callbacks publish.
    qRegisterMetaType<std::shared_ptr<const GameSnapshot>>();

    // The two prompts. Registering is done in one place or it is done
    // inconsistently.
    //
    // AND NOTHING CHECKS THESE TWO, which C1's review established by measurement
    // rather than by argument: remove both of these AND both Q_DECLARE_METATYPE
    // lines in Prompts.h, and HumanSeatTests still delivers both payloads across
    // the thread boundary and passes. Qt 6 builds a QMetaType from any complete
    // type, so the queued relay resolves them unaided on this platform - which
    // means no test can fail on their absence here, and one written to try would
    // be asserting that a working mechanism does not work.
    //
    // They stay anyway, for the build where auto-registration does not reach.
    // The failure there is a line on stderr, no crash, and a game that hangs the
    // moment it is your turn - so what defends them is that all four
    // declarations sit in two named places, and this comment.
    qRegisterMetaType<BetPrompt>();
    qRegisterMetaType<PlayPrompt>();
    qRegisterMetaType<RoundResult>();

    qRegisterMetaType<romanian_whist::Card>();

    // Easy to miss, because it is the only payload that is not one of this
    // client's own types: gameOver() carries TWO of these across the boundary.
    qRegisterMetaType<std::vector<romanian_whist::Standing>>();
}
