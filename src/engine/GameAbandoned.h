#ifndef GAME_ABANDONED_H
#define GAME_ABANDONED_H

#include <exception>

// Thrown out of a parked QtMoveProvider when the window closes on a human's
// turn. The name is the engine README's, under "Abandoning a game whose
// provider is parked", and matching it is worth doing - that section is the
// contract this implements.
//
// WHY A THROW AND NOT A RETURN VALUE: IMoveProvider has no non-throwing way to
// say "abandon". Every other return from playCard() is a move, and
// std::nullopt means "no legal play", which the engine treats as a bug. So the
// only way out of a parked provider is to wake it and throw.
//
// It propagates through GameEngine::run() and must be caught in the thread's
// run slot. Per the engine's own contract an engine that throws out of run() is
// NOT resumable: catch outside run(), destroy the engine, and restore whatever
// was set up before the call, because nothing else will.
//
// Note this is the exception for a provider parked on a HUMAN. A window closed
// during a bot's turn is requestStop()'s job and ends through onGameStopped()
// instead - GameBridge::pace() returns on cancellation rather than throwing,
// precisely so that stays the common path. Seeing this in a spectator game
// means pacing has been implemented backwards.
struct GameAbandoned : std::exception
{
    const char* what() const noexcept override
    {
        return "the game was abandoned while a move provider was parked";
    }
};

#endif
