#include <catch2/catch_test_macros.hpp>

#include "paint/Avatars.h"

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
            QImageReader reader(avatar.path);
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

TEST_CASE("There is an avatar of each gender", "[avatars]")
{
    bool male = false;
    bool female = false;

    for(const avatars::Avatar& avatar : avatars::catalogue())
    {
        male = male || avatar.gender == Gender::Male;
        female = female || avatar.gender == Gender::Female;
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

// ---- the owner's rule: one avatar per player, at random, of their gender ----

namespace
{
// Three men's faces and two women's, so a test can run either gender out.
std::vector<avatars::Avatar> madeUp()
{
    return { { "m1", Gender::Male, QString() },
             { "f1", Gender::Female, QString() },
             { "m2", Gender::Male, QString() },
             { "m3", Gender::Male, QString() },
             { "f2", Gender::Female, QString() } };
}

Gender genderOf(const std::string& id)
{
    return id.front() == 'f' ? Gender::Female : Gender::Male;
}
}

TEST_CASE("Every player is given an avatar of their own gender", "[avatars]")
{
    const std::vector<Gender> genders = { Gender::Male, Gender::Female, Gender::Female,
                                          Gender::Male, Gender::Male, Gender::Female, Gender::Male };

    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(genders, madeUp(), random);

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
        const std::vector<std::string> chosen = avatars::assign(genders, madeUp(), random);

        INFO("seed " << seed);
        REQUIRE(std::set<std::string>(chosen.begin(), chosen.end()).size() == chosen.size());
    }
}

TEST_CASE("A gender that runs out of avatars starts again from all of them", "[avatars]")
{
    // FIVE WOMEN AND TWO WOMEN'S FACES, with men between them, whose picks must not
    // touch the women's pool. Each pair of women takes both faces, one each,
    // before any face is worn a second time; the fifth wears one of the two.
    const std::vector<Gender> genders = { Gender::Female, Gender::Male, Gender::Female, Gender::Female,
                                          Gender::Male, Gender::Female, Gender::Female };

    for(std::uint32_t seed = 0 ; seed < 50 ; seed++)
    {
        std::mt19937 random(seed);
        const std::vector<std::string> chosen = avatars::assign(genders, madeUp(), random);

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
    const std::vector<Gender> genders = { Gender::Male, Gender::Female, Gender::Male, Gender::Male };

    std::mt19937 once(1234);
    std::mt19937 again(1234);

    REQUIRE(avatars::assign(genders, madeUp(), once) == avatars::assign(genders, madeUp(), again));
}

TEST_CASE("Every avatar of a gender is sometimes the one chosen", "[avatars]")
{
    // RANDOM, NOT THE CATALOGUE'S FIRST: over many seeds, the first man and the
    // first woman at the table each wear every face of their gender at least once.
    std::map<std::string, int> worn;

    for(std::uint32_t seed = 0 ; seed < 200 ; seed++)
    {
        std::mt19937 random(seed);

        for(const std::string& id : avatars::assign({ Gender::Male, Gender::Female }, madeUp(), random))
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
    const std::vector<avatars::Avatar> menOnly = { { "m1", Gender::Male, QString() } };

    std::mt19937 random(5);
    const std::vector<std::string> chosen = avatars::assign({ Gender::Female, Gender::Male, Gender::Female },
                                                            menOnly, random);

    REQUIRE(chosen == std::vector<std::string>{ "", "m1", "" });
}
