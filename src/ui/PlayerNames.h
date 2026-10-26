#ifndef PLAYER_NAMES_H
#define PLAYER_NAMES_H

#include "core/PlayerProfile.h"
#include "paint/Avatars.h"
#include "ui/LanguageDialog.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <random>
#include <string>
#include <vector>

// THE NAMES NEW GAME GIVES THE BOTS: resources/names.qrc, :/names/player_names.json.
//
// Drawn at random by the rules the avatars follow (the owner's call, 2026-10-23):
// a name of the bot's gender, from its strategy's own names when it has any, in the
// app's language. Drawn in the dialog, where they can still be changed, and never
// twice at one table - the engine refuses two equal names.
namespace playernames
{
struct Name
{
    QString name;

    // A man's, a woman's, or - empty, "any" in the list - either's.
    std::optional<Gender> gender;

    // The language it is drawn in; empty ("any") for both.
    std::optional<language::Language> language;

    // Dedicated to one bot strategy when set, as an avatar can be: newgame::
    // avatarStrategyOf()'s keys. Empty for a name any bot of its gender can have.
    std::string strategy;
};

// Whether somebody of this gender, in this language, can have it.
bool suits(const Name& name, Gender gender, language::Language language);

// A list's entries from its JSON, as catalogue() reads player_names.json. An entry
// with no name, a name past newgame::MaximumNameLength, a gender other than "male",
// "female" or "any", a language other than "ro", "en" or "any", a strategy that is
// not text, or the same name in the same language twice is skipped with a warning;
// PlayerNamesTests fails on any such entry in the real list.
std::vector<Name> parse(const QByteArray& json);

// The list, read once.
const std::vector<Name>& catalogue();

// ONE NAME FOR A BOT, by the owner's rules, none of `taken` (compared ignoring
// case, as New Game compares names):
//
// 1. One of the names dedicated to its strategy that suit its gender and the
//    language, at random, if any is free.
// 2. Otherwise one of the names dedicated to no strategy that suit them.
// 3. Otherwise a placeholder, "Bot 3" from `seatNumber` up, the first that is free.
//
// The list is a parameter so a test can hand it one of its own.
QString draw(const avatars::Wearer& bot, language::Language language, const std::vector<Name>& names,
             const QStringList& taken, int seatNumber, std::mt19937& random);
}

#endif
