#ifndef PLAYER_PROFILE_H
#define PLAYER_PROFILE_H

#include <string>

// Chosen in New Game, never guessed from the name: names are typed freely, and
// a name says nothing reliable about who it belongs to.
//
// Romanian needs it: an adjective agrees with the person it describes, so the
// trick panel says "Ana e prima" but "Bogdan e primul" (persontext::).
enum class Gender
{
    Male,
    Female
};

// WHO SITS IN A SEAT, as the client knows them: everything about a player that
// is not the game. Built by New Game, carried with the deal (GameDeal::profiles)
// and stamped onto every snapshot's SeatView by GameBridge.
//
// NOT IN THE ENGINE, whose Player knows only a name. The engine is the rules and
// is shared with the terminal client; nothing here changes how a hand is played
// or scored. The name is the one field both hold: the engine needs it (it
// rejects two equal names, and the Cyborg finds its own seat by it), so
// newgame::buildDeal() hands it over from here.
//
// A nationality and an avatar are to join it, so a player's look stays in one
// place and cannot contradict their name or gender.
struct PlayerProfile
{
    // UTF-8, as the engine takes it (QString::toStdString()).
    std::string name;
    Gender gender = Gender::Male;

    bool operator==(const PlayerProfile&) const = default;
};

#endif
