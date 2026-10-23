#include "Avatars.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QtDebug>

#include <algorithm>

namespace
{
std::vector<avatars::Avatar> load()
{
    std::vector<avatars::Avatar> found;

    QFile manifest(QStringLiteral(":/avatars/avatars.json"));

    if(!manifest.open(QIODevice::ReadOnly))
    {
        qWarning() << "avatars: no catalogue at" << manifest.fileName();
        return found;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &error);

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

        const bool known = gender == QStringLiteral("male") || gender == QStringLiteral("female");
        const bool taken = std::any_of(found.begin(), found.end(), [&](const avatars::Avatar& avatar)
                                       { return avatar.id == id.toStdString(); });

        if(id.isEmpty() || !known || file.isEmpty() || taken)
        {
            qWarning() << "avatars: skipping catalogue entry" << entry;
            continue;
        }

        found.push_back(avatars::Avatar{ id.toStdString(),
                                         gender == QStringLiteral("female") ? Gender::Female : Gender::Male,
                                         QStringLiteral(":/avatars/") + file });
    }

    return found;
}
}

const std::vector<avatars::Avatar>& avatars::catalogue()
{
    static const std::vector<Avatar> entries = load();
    return entries;
}

std::vector<std::string> avatars::assign(const std::vector<Gender>& genders, const std::vector<Avatar>& catalogue,
                                         std::mt19937& random)
{
    // Every avatar of one gender, in catalogue order.
    const auto all = [&](Gender gender)
    {
        std::vector<std::string> ids;

        for(const Avatar& avatar : catalogue)
            if(avatar.gender == gender)
                ids.push_back(avatar.id);

        return ids;
    };

    std::vector<std::string> left[2] = { all(Gender::Male), all(Gender::Female) };

    std::vector<std::string> chosen;
    chosen.reserve(genders.size());

    for(const Gender gender : genders)
    {
        std::vector<std::string>& pool = left[gender == Gender::Female ? 1 : 0];

        // RUN OUT: more players of this gender than avatars. Start again.
        if(pool.empty())
            pool = all(gender);

        if(pool.empty())
        {
            chosen.emplace_back();
            continue;
        }

        std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
        const std::size_t index = pick(random);

        chosen.push_back(pool[index]);
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(index));
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
