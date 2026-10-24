#include <catch2/catch_test_macros.hpp>

#include "ui/LanguageDialog.h"
#include "ui/NewGameDialog.h"
#include "ui/PlayerNames.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

// THE NAMES NEW GAME DRAWS (resources/names.qrc, :/names/player_names.json), and
// the owner's rules for drawing one: the bot's gender and language, its strategy's
// own names when it has any free, and never a name already at the table.

using language::Language;
using avatars::Wearer;

// ---- the list ----

TEST_CASE("Every entry in the names list is read", "[names]")
{
    // A BAD ENTRY IS SKIPPED with a warning at run time; here it fails.
    QFile list(QStringLiteral(":/names/player_names.json"));
    REQUIRE(list.open(QIODevice::ReadOnly));

    const QJsonDocument document = QJsonDocument::fromJson(list.readAll());
    REQUIRE(document.isArray());

    REQUIRE(playernames::catalogue().size() == static_cast<std::size_t>(document.array().size()));
}

TEST_CASE("Every dedicated name names a strategy the game has", "[names]")
{
    std::set<std::string> known;

    for(const newgame::BotLevel level : newgame::BotLevels)
        known.insert(newgame::avatarStrategyOf(level));

    for(const playernames::Name& name : playernames::catalogue())
    {
        INFO(name.name.toStdString() << " for " << name.strategy);

        if(!name.strategy.empty())
            REQUIRE(known.contains(name.strategy));
    }
}

TEST_CASE("Each language has names enough for the largest table of one gender", "[names]")
{
    // A WATCHED GAME SEATS SIX BOTS, and they may all be men or all women: six names
    // each, or the sixth would be a "Bot 6".
    for(const Language language : { Language::English, Language::Romanian })
    {
        for(const Gender gender : { Gender::Male, Gender::Female })
        {
            std::size_t count = 0;

            for(const playernames::Name& name : playernames::catalogue())
                if(name.strategy.empty() && playernames::suits(name, gender, language))
                    count++;

            INFO(language::codeOf(language).toStdString() << ", " << (gender == Gender::Male ? "men" : "women"));
            REQUIRE(count >= newgame::MaximumSeats);
        }
    }
}

TEST_CASE("A names list entry can be for either gender, either language and one strategy", "[names]")
{
    const std::vector<playernames::Name> read = playernames::parse(R"([
        { "name": "Alex", "gender": "any", "language": "any" },
        { "name": "T-800", "gender": "male", "language": "en", "strategy": "cyborg" },
        { "name": "Ioana", "gender": "female", "language": "ro" },
        { "name": "Nobody", "gender": "both", "language": "en" },
        { "name": "Nowhere", "gender": "male", "language": "fr" },
        { "name": "Bartholomew Fairbanks III", "gender": "male", "language": "en" },
        { "name": "Number", "gender": "male", "language": "en", "strategy": 5 },
        { "name": "", "gender": "male", "language": "en" },
        { "name": "ioana", "gender": "female", "language": "ro" },
        { "name": "Ioana", "gender": "female", "language": "en" }
    ])");

    REQUIRE(read.size() == 4);

    REQUIRE(read[0].name == QStringLiteral("Alex"));
    REQUIRE(playernames::suits(read[0], Gender::Male, Language::Romanian));
    REQUIRE(playernames::suits(read[0], Gender::Female, Language::English));

    REQUIRE(read[1].strategy == "cyborg");
    REQUIRE(read[1].language == Language::English);
    REQUIRE_FALSE(playernames::suits(read[1], Gender::Male, Language::Romanian));

    REQUIRE(read[2].gender == Gender::Female);
    REQUIRE(read[2].language == Language::Romanian);

    // The same name in another language is another entry.
    REQUIRE(read[3].name == QStringLiteral("Ioana"));
    REQUIRE(read[3].language == Language::English);
}

// ---- the owner's rules ----

namespace
{
std::vector<playernames::Name> madeUp()
{
    return { { "Adam", Gender::Male, Language::English, "" },
             { "Bella", Gender::Female, Language::English, "" },
             { "Carl", Gender::Male, Language::English, "" },
             { "Dora", Gender::Female, Language::English, "" },
             { "Andrei", Gender::Male, Language::Romanian, "" },
             { "Alex", std::nullopt, std::nullopt, "" },
             { "T-800", Gender::Male, std::nullopt, "cyborg" },
             { "T-1000", Gender::Male, std::nullopt, "cyborg" } };
}

const Wearer man{ Gender::Male, "low-risk" };
const Wearer woman{ Gender::Female, "low-risk" };
const Wearer cyborg{ Gender::Male, "cyborg" };
}

TEST_CASE("A bot is given a name of its gender and the app's language", "[names]")
{
    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);

        const QString his = playernames::draw(man, Language::English, madeUp(), {}, 2, random);
        const QString hers = playernames::draw(woman, Language::English, madeUp(), {}, 2, random);
        const QString romanian = playernames::draw(man, Language::Romanian, madeUp(), {}, 2, random);

        INFO("seed " << seed);
        REQUIRE(QStringList{ "Adam", "Carl", "Alex" }.contains(his));
        REQUIRE(QStringList{ "Bella", "Dora", "Alex" }.contains(hers));
        REQUIRE(QStringList{ "Andrei", "Alex" }.contains(romanian));
    }
}

TEST_CASE("A strategy's own names go to its bots and to nobody else", "[names]")
{
    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);

        INFO("seed " << seed);
        REQUIRE(QStringList{ "T-800", "T-1000" }.contains(
            playernames::draw(cyborg, Language::English, madeUp(), {}, 2, random)));
        REQUIRE_FALSE(QStringList{ "T-800", "T-1000" }.contains(
            playernames::draw(man, Language::English, madeUp(), {}, 2, random)));
    }

    // A FEMALE CYBORG, and the cyborg names a man's: an ordinary woman's name.
    std::mt19937 random(1);
    REQUIRE(QStringList{ "Bella", "Dora", "Alex" }.contains(
        playernames::draw(Wearer{ Gender::Female, "cyborg" }, Language::English, madeUp(), {}, 2, random)));
}

TEST_CASE("A name already at the table is never drawn again", "[names]")
{
    // COMPARED IGNORING CASE, as New Game compares names: "adam" is Adam taken.
    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const QString drawn = playernames::draw(man, Language::English, madeUp(), { "adam", "ALEX" }, 2, random);

        INFO("seed " << seed);
        REQUIRE(drawn == QStringLiteral("Carl"));
    }
}

TEST_CASE("When a group runs out the next is drawn from and then a placeholder", "[names]")
{
    std::mt19937 random(7);

    // A strategy's names all taken: an ordinary one of its gender.
    REQUIRE(QStringList{ "Adam", "Carl", "Alex" }.contains(
        playernames::draw(cyborg, Language::English, madeUp(), { "T-800", "T-1000" }, 2, random)));

    // Every one taken: "Bot 3" from the seat's number, or the next that is free.
    const QStringList everyMan{ "T-800", "T-1000", "Adam", "Carl", "Alex" };
    REQUIRE(playernames::draw(cyborg, Language::English, madeUp(), everyMan, 3, random) == QStringLiteral("Bot 3"));
    REQUIRE(playernames::draw(man, Language::English, madeUp(), everyMan + QStringList{ "Bot 3" }, 3, random)
            == QStringLiteral("Bot 4"));
}

TEST_CASE("Every name of a group is sometimes the one drawn", "[names]")
{
    // RANDOM, NOT THE LIST'S FIRST.
    std::map<QString, int> drawn;

    for(std::uint32_t seed = 0 ; seed < 200 ; seed++)
    {
        std::mt19937 random(seed);
        drawn[playernames::draw(woman, Language::English, madeUp(), {}, 2, random)]++;
    }

    for(const char* name : { "Bella", "Dora", "Alex" })
    {
        INFO(name);
        REQUIRE(drawn[QString::fromUtf8(name)] > 0);
    }
}

TEST_CASE("The same seed draws the same name", "[names]")
{
    std::mt19937 once(99);
    std::mt19937 again(99);

    REQUIRE(playernames::draw(man, Language::English, madeUp(), {}, 2, once)
            == playernames::draw(man, Language::English, madeUp(), {}, 2, again));
}
