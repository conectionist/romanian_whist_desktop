#include <catch2/catch_test_macros.hpp>

#include "paint/Avatars.h"
#include "ui/NewGameDialog.h"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QString>
#include <QSvgRenderer>

#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

// THE PORTRAITS' CATALOGUE (resources/avatars.qrc, :/avatars/avatars.json). New
// avatars arrive as files plus a line in the catalogue, so what these check is
// that the two agree: every entry is read, names a file that is there and loads,
// and every file is named by an entry.

TEST_CASE("Every entry in the avatar catalogue is read", "[avatars]")
{
    // A BAD ENTRY IS SKIPPED with a warning at run time, so the app still starts.
    // Here it fails: the catalogue must hold exactly what the manifest lists.
    QFile manifest(QStringLiteral(":/avatars/avatars.json"));
    REQUIRE(manifest.open(QIODevice::ReadOnly));

    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    REQUIRE(document.isArray());

    REQUIRE(avatars::catalogue().size() == static_cast<std::size_t>(document.array().size()));
}

TEST_CASE("The table mock-up's six busts come first and in order", "[avatars]")
{
    // The owner's call, 2026-10-23: the busts the client drew in code until then
    // are the catalogue's first six, in the order the table wore them by place.
    const std::vector<std::pair<std::string, Gender>> six = {
        { "man-dark-hair-beard", Gender::Male },
        { "man-hat-mustache", Gender::Male },
        { "woman-long-brown-hair", Gender::Female },
        { "man-white-hair-hat", Gender::Male },
        { "woman-long-red-hair", Gender::Female },
        { "man-black-hair-beard", Gender::Male },
    };

    const std::vector<avatars::Avatar>& catalogue = avatars::catalogue();
    REQUIRE(catalogue.size() >= six.size());

    for(std::size_t i = 0 ; i < six.size() ; i++)
    {
        INFO("entry " << i);
        REQUIRE(catalogue[i].id == six[i].first);
        REQUIRE(catalogue[i].gender == six[i].second);
    }
}

TEST_CASE("Every avatar's id is its own and its file loads", "[avatars]")
{
    std::set<std::string> ids;

    for(const avatars::Avatar& avatar : avatars::catalogue())
    {
        INFO("avatar " << avatar.id << " at " << avatar.path.toStdString());

        REQUIRE_FALSE(avatar.id.empty());
        REQUIRE(ids.insert(avatar.id).second);

        // SVG or PNG, the two the app draws without an image plugin.
        const bool svg = avatar.path.endsWith(QStringLiteral(".svg"));
        const bool png = avatar.path.endsWith(QStringLiteral(".png"));
        REQUIRE((svg || png));

        if(svg)
        {
            QSvgRenderer renderer(avatar.path);
            REQUIRE(renderer.isValid());
            REQUIRE_FALSE(renderer.viewBoxF().isEmpty());
        }
        else
        {
            // A PNG INSIDE, NOT ONLY BY NAME: a JPEG saved as .png decodes here,
            // where the jpeg plugin is installed, and shows the bare box in the
            // shipped app, whose deploy step strips every image plugin. PNG is
            // built into Qt.
            QImageReader reader(avatar.path);
            reader.setDecideFormatFromContent(true);
            REQUIRE(reader.format() == QByteArrayLiteral("png"));
            REQUIRE_FALSE(reader.read().isNull());
        }
    }
}

TEST_CASE("Every file under the avatars is in the catalogue", "[avatars]")
{
    // The other half of adding one: a file in avatars.qrc that no catalogue entry
    // names is art nobody can wear.
    QSet<QString> named;

    for(const avatars::Avatar& avatar : avatars::catalogue())
        named.insert(avatar.path);

    const QStringList files = QDir(QStringLiteral(":/avatars")).entryList(QDir::Files);
    REQUIRE_FALSE(files.isEmpty());

    for(const QString& file : files)
    {
        if(file == QStringLiteral("avatars.json"))
            continue;

        INFO("file " << file.toStdString());
        REQUIRE(named.contains(QStringLiteral(":/avatars/") + file));
    }
}

TEST_CASE("There is an avatar anyone of either gender can wear", "[avatars]")
{
    bool male = false;
    bool female = false;

    for(const avatars::Avatar& avatar : avatars::catalogue())
    {
        // Dedicated to nobody, so anyone of that gender can be given it.
        if(!avatar.strategy.empty())
            continue;

        male = male || avatars::suits(avatar, Gender::Male);
        female = female || avatars::suits(avatar, Gender::Female);
    }

    REQUIRE(male);
    REQUIRE(female);
}

TEST_CASE("An avatar is found by its id and by nothing else", "[avatars]")
{
    const avatars::Avatar* found = avatars::find("woman-long-red-hair");
    REQUIRE(found != nullptr);
    REQUIRE(found->gender == Gender::Female);
    REQUIRE(found->path == QStringLiteral(":/avatars/woman-long-red-hair.svg"));

    REQUIRE(avatars::find("no-such-avatar") == nullptr);
    REQUIRE(avatars::find("") == nullptr);
}

TEST_CASE("A catalogue entry can be for either gender and for one strategy", "[avatars]")
{
    // What the owner's avatars will say: "any" for a face anybody can wear (a
    // star), "strategy" for one a bot of that strategy wears (a cyborg). An entry
    // that cannot be read is skipped, the rest kept.
    const std::vector<avatars::Avatar> read = avatars::parse(R"([
        { "id": "star", "gender": "any", "file": "star.svg" },
        { "id": "cyborg", "gender": "male", "strategy": "cyborg", "file": "cyborg.png" },
        { "id": "robot", "gender": "any", "strategy": "cyborg", "file": "robot.svg" },
        { "id": "nobody", "gender": "both", "file": "nobody.svg" },
        { "id": "number", "gender": "male", "strategy": 5, "file": "number.svg" },
        { "id": "star", "gender": "female", "file": "again.svg" },
        { "id": "plain", "gender": "female", "file": "plain.svg" }
    ])");

    REQUIRE(read.size() == 4);

    REQUIRE(read[0].id == "star");
    REQUIRE_FALSE(read[0].gender.has_value());
    REQUIRE(read[0].strategy.empty());
    REQUIRE(read[0].path == QStringLiteral(":/avatars/star.svg"));
    REQUIRE(avatars::suits(read[0], Gender::Male));
    REQUIRE(avatars::suits(read[0], Gender::Female));

    REQUIRE(read[1].id == "cyborg");
    REQUIRE(read[1].gender == Gender::Male);
    REQUIRE(read[1].strategy == "cyborg");
    REQUIRE_FALSE(avatars::suits(read[1], Gender::Female));

    REQUIRE(read[2].id == "robot");
    REQUIRE_FALSE(read[2].gender.has_value());
    REQUIRE(read[2].strategy == "cyborg");

    REQUIRE(read[3].id == "plain");
    REQUIRE(read[3].gender == Gender::Female);
}

TEST_CASE("Every dedicated avatar names a strategy the game has", "[avatars]")
{
    // A misspelt strategy ("cyborgs") would make an avatar nobody can ever wear.
    std::set<std::string> known;

    for(const newgame::BotLevel level : newgame::BotLevels)
        known.insert(newgame::avatarStrategyOf(level));

    for(const avatars::Avatar& avatar : avatars::catalogue())
    {
        INFO("avatar " << avatar.id << " for " << avatar.strategy);

        if(!avatar.strategy.empty())
            REQUIRE(known.contains(avatar.strategy));
    }
}

// ---- the owner's rules: one avatar per player, at random, of their gender, and
// ---- their strategy's own if it has any

namespace
{
using avatars::Wearer;

const Wearer man{ Gender::Male, "" };
const Wearer woman{ Gender::Female, "" };

// Three men's faces and two women's, for anyone: so a test can run either
// gender out.
std::vector<avatars::Avatar> madeUp()
{
    return { { "m1", Gender::Male, "", QString() },
             { "f1", Gender::Female, "", QString() },
             { "m2", Gender::Male, "", QString() },
             { "m3", Gender::Male, "", QString() },
             { "f2", Gender::Female, "", QString() } };
}

// The same, and a male cyborg's face and one for any Reckoner Alpha.
std::vector<avatars::Avatar> withDedicated()
{
    std::vector<avatars::Avatar> catalogue = madeUp();
    catalogue.push_back({ "cyborg-m", Gender::Male, "cyborg", QString() });
    catalogue.push_back({ "alpha-any", std::nullopt, "reckoner-alpha", QString() });
    return catalogue;
}

Gender genderOf(const std::string& id)
{
    return id.front() == 'f' ? Gender::Female : Gender::Male;
}

std::vector<Wearer> people(const std::vector<Gender>& genders)
{
    std::vector<Wearer> wearers;

    for(const Gender gender : genders)
        wearers.push_back(Wearer{ gender, "" });

    return wearers;
}
}

TEST_CASE("Every player is given an avatar of their own gender", "[avatars]")
{
    const std::vector<Gender> genders = { Gender::Male, Gender::Female, Gender::Female,
                                          Gender::Male, Gender::Male, Gender::Female, Gender::Male };

    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(people(genders), madeUp(), random);

        REQUIRE(chosen.size() == genders.size());

        for(std::size_t i = 0 ; i < genders.size() ; i++)
        {
            INFO("seed " << seed << ", player " << i);
            REQUIRE_FALSE(chosen[i].empty());
            REQUIRE(genderOf(chosen[i]) == genders[i]);
        }
    }
}

TEST_CASE("No two players share an avatar while there are enough", "[avatars]")
{
    // Three men and two women: exactly the faces there are.
    const std::vector<Gender> genders = { Gender::Female, Gender::Male, Gender::Male,
                                          Gender::Female, Gender::Male };

    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(people(genders), madeUp(), random);

        INFO("seed " << seed);
        REQUIRE(std::set<std::string>(chosen.begin(), chosen.end()).size() == chosen.size());
    }
}

TEST_CASE("A gender that runs out of avatars starts again from all of them", "[avatars]")
{
    // FIVE WOMEN AND TWO WOMEN'S FACES, with men between them, whose picks must not
    // touch the women's faces. Each pair of women takes both faces, one each,
    // before any face is worn a second time; the fifth wears one of the two.
    const std::vector<Gender> genders = { Gender::Female, Gender::Male, Gender::Female, Gender::Female,
                                          Gender::Male, Gender::Female, Gender::Female };

    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(people(genders), madeUp(), random);

        std::vector<std::string> women;

        for(std::size_t i = 0 ; i < genders.size() ; i++)
            if(genders[i] == Gender::Female)
                women.push_back(chosen[i]);

        INFO("seed " << seed);
        REQUIRE(std::set<std::string>{ women[0], women[1] } == std::set<std::string>{ "f1", "f2" });
        REQUIRE(std::set<std::string>{ women[2], women[3] } == std::set<std::string>{ "f1", "f2" });
        REQUIRE(genderOf(women[4]) == Gender::Female);
    }
}

TEST_CASE("The same seed gives the same avatars", "[avatars]")
{
    const std::vector<Wearer> wearers = { man, woman, Wearer{ Gender::Male, "cyborg" }, man };

    std::mt19937 once(1234);
    std::mt19937 again(1234);

    REQUIRE(avatars::assign(wearers, withDedicated(), once) == avatars::assign(wearers, withDedicated(), again));
}

TEST_CASE("Every avatar of a gender is sometimes the one chosen", "[avatars]")
{
    // RANDOM, NOT THE CATALOGUE'S FIRST: over many seeds, the first man and the
    // first woman at the table each wear every face of their gender at least once.
    std::map<std::string, int> worn;

    for(std::uint32_t seed = 0 ; seed < 200 ; seed++)
    {
        std::mt19937 random(seed);

        for(const std::string& id : avatars::assign({ man, woman }, madeUp(), random))
            worn[id]++;
    }

    for(const avatars::Avatar& avatar : madeUp())
    {
        INFO("avatar " << avatar.id);
        REQUIRE(worn[avatar.id] > 0);
    }
}

TEST_CASE("A gender with no avatars at all gets the bare box", "[avatars]")
{
    const std::vector<avatars::Avatar> menOnly = { { "m1", Gender::Male, "", QString() } };

    std::mt19937 random(5);
    const std::vector<std::string> chosen = avatars::assign({ woman, man, woman }, menOnly, random);

    REQUIRE(chosen == std::vector<std::string>{ "", "m1", "" });
}

TEST_CASE("A strategy's own avatar goes to its bots and to nobody else", "[avatars]")
{
    // THE OWNER'S FIRST RULE: a Cyborg wears the cyborg face when the catalogue has
    // one of his gender, and nobody else ever does - not you, and not a bot that
    // plays another strategy, even when the ordinary faces have run out.
    const std::vector<Wearer> wearers = { man, Wearer{ Gender::Male, "low-risk" }, Wearer{ Gender::Male, "cyborg" },
                                          man, man, Wearer{ Gender::Male, "ducking" } };

    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(wearers, withDedicated(), random);

        INFO("seed " << seed);

        for(std::size_t i = 0 ; i < wearers.size() ; i++)
        {
            if(wearers[i].strategy == "cyborg")
                REQUIRE(chosen[i] == "cyborg-m");
            else
                REQUIRE((chosen[i] == "m1" || chosen[i] == "m2" || chosen[i] == "m3"));
        }
    }
}

TEST_CASE("A strategy's avatars of the other gender are not a bot's", "[avatars]")
{
    // A FEMALE CYBORG, and the only cyborg face a man's: she wears an ordinary
    // woman's face, as though the strategy had none (the owner's call).
    for(std::uint32_t seed = 0 ; seed < 20 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign({ Wearer{ Gender::Female, "cyborg" } },
                                                                withDedicated(), random);

        REQUIRE((chosen.front() == "f1" || chosen.front() == "f2"));
    }
}

TEST_CASE("Two bots of one strategy share its only avatar", "[avatars]")
{
    // RUN OUT, AS ANY GROUP CAN: two Cyborgs and one cyborg face, and both wear it
    // (the owner's call) rather than the second taking an ordinary face.
    std::mt19937 random(9);
    const std::vector<std::string> chosen = avatars::assign({ Wearer{ Gender::Male, "cyborg" }, man,
                                                              Wearer{ Gender::Male, "cyborg" } },
                                                            withDedicated(), random);

    REQUIRE(chosen[0] == "cyborg-m");
    REQUIRE(chosen[2] == "cyborg-m");
    REQUIRE(chosen[1] != "cyborg-m");
}

TEST_CASE("A Reckoner preset's avatar is only that preset's", "[avatars]")
{
    // Each preset is a strategy of its own to the avatars (the owner's call): the
    // Alpha's face is an Alpha's, of either gender, and never a Delta's.
    std::mt19937 random(3);
    const std::vector<std::string> chosen = avatars::assign({ Wearer{ Gender::Female, "reckoner-alpha" },
                                                              Wearer{ Gender::Male, "reckoner-delta" } },
                                                            withDedicated(), random);

    REQUIRE(chosen[0] == "alpha-any");
    REQUIRE(chosen[1] != "alpha-any");
    REQUIRE(genderOf(chosen[1]) == Gender::Male);
}

TEST_CASE("A gender-neutral avatar is worn by either gender but by one player at a time", "[avatars]")
{
    // "any" suits a man and a woman alike, so it is in both genders' faces - and
    // whoever takes it first takes it from both, while either has another.
    std::vector<avatars::Avatar> catalogue = { { "m1", Gender::Male, "", QString() },
                                               { "star", std::nullopt, "", QString() },
                                               { "f1", Gender::Female, "", QString() },
                                               { "m2", Gender::Male, "", QString() },
                                               { "f2", Gender::Female, "", QString() } };

    std::map<std::string, std::set<Gender>> wornBy;

    for(std::uint32_t seed = 0 ; seed < 200 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<Wearer> wearers = { man, woman, man, woman };
        const std::vector<std::string> chosen = avatars::assign(wearers, catalogue, random);

        INFO("seed " << seed);

        // Two of each gender's own and the star between them: never a repeat.
        REQUIRE(std::set<std::string>(chosen.begin(), chosen.end()).size() == chosen.size());

        for(std::size_t i = 0 ; i < wearers.size() ; i++)
            wornBy[chosen[i]].insert(wearers[i].gender);
    }

    REQUIRE(wornBy["star"] == std::set<Gender>{ Gender::Male, Gender::Female });
}

TEST_CASE("A gender that runs out does not free a gender-neutral avatar the other wears", "[avatars]")
{
    // PR #40's review, finding #1. The two men run out of faces (m1 and the star);
    // starting over must not free the star for the second woman while a woman
    // already wears it and f1 and f2 are both free. The rule used to erase the men's
    // whole group, the star with it, and she drew it one deal in eighteen.
    const std::vector<avatars::Avatar> catalogue = { { "m1", Gender::Male, "", QString() },
                                                     { "star", std::nullopt, "", QString() },
                                                     { "f1", Gender::Female, "", QString() },
                                                     { "f2", Gender::Female, "", QString() } };

    const std::vector<Wearer> wearers = { woman, man, man, woman };
    bool starWornFirst = false;

    for(std::uint32_t seed = 0 ; seed < 200 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(wearers, catalogue, random);

        INFO("seed " << seed);
        REQUIRE(chosen[0] != chosen[3]);

        starWornFirst = starWornFirst || chosen[0] == "star";
    }

    // The case the test is about came up.
    REQUIRE(starWornFirst);
}
