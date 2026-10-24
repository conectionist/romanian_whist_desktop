#ifndef AVATARS_H
#define AVATARS_H

#include "core/PlayerProfile.h"

#include <QByteArray>
#include <QString>

#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// THE PORTRAITS A SEAT CAN WEAR, as files: resources/avatars.qrc, listed by
// :/avatars/avatars.json. Theme::avatar() draws one by its id.
//
// FILES, NOT DRAWING CODE, since the owner's call of 2026-10-23: new avatars come
// as SVG or PNG art, and adding one is a file, a line in the .qrc and an entry in
// the catalogue. The first six are the table mock-up's busts, which the client
// drew in code until then (tools/export_portraits.py writes them).
namespace avatars
{
struct Avatar
{
    // What a PlayerProfile names it by. Stable across reordering the catalogue.
    std::string id;

    // Who can wear it: a man, a woman, or - empty, "any" in the catalogue - either.
    std::optional<Gender> gender;

    // DEDICATED TO ONE BOT STRATEGY when set: worn only by a bot that plays it,
    // and by nobody else, the player included. Empty for an avatar anyone of its
    // gender may wear. The keys are newgame::avatarStrategyOf()'s.
    std::string strategy;

    // Its resource path, ":/avatars/<file>".
    QString path;
};

// Whether somebody of this gender can wear it.
bool suits(const Avatar& avatar, Gender gender);

// The catalogue, in the manifest's order, read once. An entry with no id, a
// gender other than "male", "female" or "any", a strategy that is not text, no
// file, or an id already taken is skipped with a warning rather than taking the rest down with it; AvatarTests
// fails on any such entry.
const std::vector<Avatar>& catalogue();

// The avatar with this id, or null.
const Avatar* find(std::string_view id);

// A catalogue's entries from its JSON, as catalogue() reads avatars.json: what a
// test hands a manifest of its own to. Paths are under ":/avatars/".
std::vector<Avatar> parse(const QByteArray& json);

// Who an avatar is being chosen for: a gender, and the strategy a bot plays
// (newgame::avatarStrategyOf()), empty for you.
struct Wearer
{
    Gender gender = Gender::Male;
    std::string strategy;
};

// ONE AVATAR PER PLAYER, by the owner's rules (2026-10-23). For each player in
// order:
//
// 1. WHICH AVATARS ARE THEIRS: those dedicated to the player's strategy that
//    suit their gender, if the catalogue has any; otherwise those dedicated to
//    no strategy that suit their gender. A strategy's avatars are only ever its
//    own, so a female Cyborg with only a male cyborg face gets an ordinary woman's.
// 2. One of them at random, from those nobody at the table wears yet.
// 3. IF EVERY ONE IS WORN, the pick starts over among those worn least: two
//    Cyborgs and one cyborg face, and both wear it.
//
// A gender-neutral avatar is one of either gender's, and taken by whoever gets it
// first. It counts as worn for both genders, so one gender starting over never
// frees it for the other. A player with no avatars at all gets an empty id: the
// bare box.
//
// The catalogue is a parameter so a test can hand it one of its own.
std::vector<std::string> assign(const std::vector<Wearer>& wearers, const std::vector<Avatar>& catalogue,
                                std::mt19937& random);
}

#endif
