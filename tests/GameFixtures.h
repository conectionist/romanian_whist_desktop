#ifndef GAME_FIXTURES_H
#define GAME_FIXTURES_H

#include "engine/GameBridge.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// The two things every test that drives a real game needs, in one place.
//
// GameBridgeTests and GameControllerTests each carried an identical private
// copy; A6 wanted a third with a different seat count, which is the point at
// which copying stops being cheaper than sharing.

namespace fixtures
{

// Zero pacing, so a whole 26-round game runs in milliseconds. Every publish is
// followed by one of these waits, and at 250ms a card a full schedule would be
// several minutes per test case.
inline Pacing instant()
{
    return Pacing{ std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0),
                   std::chrono::milliseconds(0) };
}

// DETERMINISTIC STRATEGIES ONLY, and never RandomCardStrategy even seeded: its
// draw count depends on what every other seat bids, so any behavioural change
// desyncs its stream and turns a located difference into "something changed
// somewhere". The engine's own golden suite excludes it for the same reason.
//
// Two seats by default because that is what the shutdown and bridge suites
// want - the fewest that make a game - and four when a caller needs a table
// with a real trick in it.
inline romanian_whist::GameSetup buildSetup(std::size_t seatCount = 2,
                                            std::optional<std::uint32_t> seed = 42u)
{
    using namespace romanian_whist;

    // Six, which is the most seats the engine accepts. A caller asking for more
    // is a test bug, and the fallback is what lets the ENGINE say so: indexing
    // past the end here would be undefined behaviour reached before
    // GameEngine::start() ever gets to validate the count.
    static const std::array<std::string, 6> names =
        { "Ana", "Bogdan", "Cristina", "Dan", "Elena", "Florin" };

    GameSetup setup;

    for(std::size_t i = 0 ; i < seatCount ; i++)
    {
        std::unique_ptr<IStrategy> strategy;

        switch(i % 3)
        {
            case 0:  strategy = std::make_unique<LowRiskStrategy>();    break;
            case 1:  strategy = std::make_unique<DuckingStrategy>();    break;
            default: strategy = std::make_unique<FirstCardStrategy>();  break;
        }

        setup.seats.push_back(SeatSetup{ i < names.size() ? names[i]
                                                          : "Seat " + std::to_string(i + 1),
                                         std::make_unique<AiMoveProvider>(std::move(strategy)) });
    }

    setup.structure = GameStructure::S_181;

    // Left at its default, which appends a Forehead round and then a Hidden
    // one. A6 asserts on both, so this is load-bearing rather than incidental.
    setup.endWithForeheadAndHidden = true;

    setup.shuffleSeed = seed;

    return setup;
}

} // namespace fixtures

#endif
