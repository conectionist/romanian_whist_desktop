#include "Avatars.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QtDebug>

#include <algorithm>
#include <map>

namespace
{
std::vector<avatars::Avatar> load()
{
    QFile manifest(QStringLiteral(":/avatars/avatars.json"));

    if(!manifest.open(QIODevice::ReadOnly))
    {
        qWarning() << "avatars: no catalogue at" << manifest.fileName();
        return {};
    }

    return avatars::parse(manifest.readAll());
}
}

std::vector<avatars::Avatar> avatars::parse(const QByteArray& json)
{
    std::vector<avatars::Avatar> found;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);

    if(!document.isArray())
    {
        qWarning() << "avatars: the catalogue is not a JSON array:" << error.errorString();
        return found;
    }

    for(const QJsonValue& value : document.array())
    {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString gender = entry.value(QStringLiteral("gender")).toString();
        const QString file = entry.value(QStringLiteral("file")).toString();

        // Optional: absent is an avatar for anyone of its gender.
        const QJsonValue strategy = entry.value(QStringLiteral("strategy"));

        const bool known = gender == QStringLiteral("male") || gender == QStringLiteral("female")
                           || gender == QStringLiteral("any");
        const bool strategyRead = strategy.isUndefined() || strategy.isString();
        const bool taken = std::any_of(found.begin(), found.end(), [&](const avatars::Avatar& avatar)
                                       { return avatar.id == id.toStdString(); });

        if(id.isEmpty() || !known || !strategyRead || file.isEmpty() || taken)
        {
            qWarning() << "avatars: skipping catalogue entry" << entry;
            continue;
        }

        std::optional<Gender> who;

        if(gender == QStringLiteral("male"))
            who = Gender::Male;
        else if(gender == QStringLiteral("female"))
            who = Gender::Female;

        found.push_back(avatars::Avatar{ id.toStdString(), who, strategy.toString().toStdString(),
                                         QStringLiteral(":/avatars/") + file });
    }

    return found;
}

const std::vector<avatars::Avatar>& avatars::catalogue()
{
    static const std::vector<Avatar> entries = load();
    return entries;
}

bool avatars::suits(const Avatar& avatar, Gender gender)
{
    return !avatar.gender || *avatar.gender == gender;
}

std::vector<std::string> avatars::assign(const std::vector<Wearer>& wearers, const std::vector<Avatar>& catalogue,
                                         std::mt19937& random)
{
    // Every avatar dedicated to `strategy` (empty: to none) that suits `gender`, in
    // catalogue order.
    const auto group = [&](const std::string& strategy, Gender gender)
    {
        std::vector<std::string> ids;

        for(const Avatar& avatar : catalogue)
            if(avatar.strategy == strategy && suits(avatar, gender))
                ids.push_back(avatar.id);

        return ids;
    };

    // HOW MANY AT THE TABLE WEAR EACH, across every group: a gender-neutral avatar
    // belongs to both genders' groups, and must not be handed to a second player
    // while either group still has another. A count, never cleared, so one group
    // starting over cannot free a face the other group's player wears (PR #40's
    // review, finding #1: a set erased the whole group when it ran out).
    std::map<std::string, unsigned int> worn;

    const auto wornCount = [&worn](const std::string& id)
    {
        const auto found = worn.find(id);
        return found == worn.end() ? 0U : found->second;
    };

    std::vector<std::string> chosen;
    chosen.reserve(wearers.size());

    for(const Wearer& wearer : wearers)
    {
        // 1. A strategy's own avatars, if it has any of this gender; otherwise the
        //    avatars dedicated to nobody.
        std::vector<std::string> theirs = wearer.strategy.empty() ? std::vector<std::string>()
                                                                  : group(wearer.strategy, wearer.gender);

        if(theirs.empty())
            theirs = group(std::string(), wearer.gender);

        if(theirs.empty())
        {
            chosen.emplace_back();
            continue;
        }

        // 2. Those worn least, in catalogue order: those nobody wears yet, while
        //    there are any.
        // 3. RUN OUT: when every one is worn, the least worn are all of them, and
        //    the pick starts over among them.
        unsigned int least = wornCount(theirs.front());

        for(const std::string& id : theirs)
            least = std::min(least, wornCount(id));

        std::vector<std::string> free;

        for(const std::string& id : theirs)
            if(wornCount(id) == least)
                free.push_back(id);

        std::uniform_int_distribution<std::size_t> pick(0, free.size() - 1);
        const std::string& id = free[pick(random)];

        chosen.push_back(id);
        ++worn[id];
    }

    return chosen;
}

const avatars::Avatar* avatars::find(std::string_view id)
{
    const std::vector<Avatar>& entries = catalogue();

    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [id](const Avatar& avatar) { return avatar.id == id; });

    return found == entries.end() ? nullptr : &*found;
}
