#ifndef AVATARS_H
#define AVATARS_H

#include "core/PlayerProfile.h"

#include <QString>

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
    Gender gender = Gender::Male;

    // Its resource path, ":/avatars/<file>".
    QString path;
};

// The catalogue, in the manifest's order, read once. An entry with no id, a
// gender other than "male" or "female", no file, or an id already taken is
// skipped with a warning rather than taking the rest down with it; AvatarTests
// fails on any such entry.
const std::vector<Avatar>& catalogue();

// The avatar with this id, or null.
const Avatar* find(std::string_view id);

// ONE AVATAR PER PLAYER, by the owner's rule (2026-10-23). For each player in
// order, take an avatar of that player's gender at random from what is left of
// that gender, and remove it, so no two players share a face while there are
// faces enough. A gender whose avatars have all been taken starts again from the
// full set. A gender the catalogue has none of gets an empty id: the bare box.
//
// The catalogue is a parameter so a test can hand it one of its own.
std::vector<std::string> assign(const std::vector<Gender>& genders, const std::vector<Avatar>& catalogue,
                                std::mt19937& random);
}

#endif
