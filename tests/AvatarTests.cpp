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
