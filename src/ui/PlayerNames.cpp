#include "PlayerNames.h"

#include "ui/NewGameDialog.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QtDebug>

#include <algorithm>

namespace
{
bool isTaken(const QString& name, const QStringList& taken)
{
    return taken.contains(name, Qt::CaseInsensitive);
}

std::vector<playernames::Name> load()
{
    QFile list(QStringLiteral(":/names/player_names.json"));

    if(!list.open(QIODevice::ReadOnly))
    {
        qWarning() << "playernames: no list at" << list.fileName();
        return {};
    }

    return playernames::parse(list.readAll());
}
}

bool playernames::suits(const Name& name, Gender gender, language::Language language)
{
    return (!name.gender || *name.gender == gender) && (!name.language || *name.language == language);
}

std::vector<playernames::Name> playernames::parse(const QByteArray& json)
{
    std::vector<Name> found;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);

    if(!document.isArray())
    {
        qWarning() << "playernames: the list is not a JSON array:" << error.errorString();
        return found;
    }

    for(const QJsonValue& value : document.array())
    {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const QString gender = entry.value(QStringLiteral("gender")).toString();
        const QString language = entry.value(QStringLiteral("language")).toString();
        const QJsonValue strategy = entry.value(QStringLiteral("strategy"));

        const bool genderRead = gender == QStringLiteral("male") || gender == QStringLiteral("female")
                                || gender == QStringLiteral("any");
        const bool languageRead = language == QStringLiteral("ro") || language == QStringLiteral("en")
                                  || language == QStringLiteral("any");
        const bool strategyRead = strategy.isUndefined() || strategy.isString();

        std::optional<language::Language> in;

        if(language != QStringLiteral("any"))
            in = language::fromCode(language);

        const bool twice = std::any_of(found.begin(), found.end(), [&](const Name& earlier)
                                       {
                                           return earlier.name.compare(name, Qt::CaseInsensitive) == 0
                                                  && earlier.language == in;
                                       });

        if(name.isEmpty() || name.size() > newgame::MaximumNameLength || !genderRead || !languageRead
           || !strategyRead || twice)
        {
            qWarning() << "playernames: skipping list entry" << entry;
            continue;
        }

        std::optional<Gender> who;

        if(gender == QStringLiteral("male"))
            who = Gender::Male;
        else if(gender == QStringLiteral("female"))
            who = Gender::Female;

        found.push_back(Name{ name, who, in, strategy.toString().toStdString() });
    }

    return found;
}

const std::vector<playernames::Name>& playernames::catalogue()
{
    static const std::vector<Name> entries = load();
    return entries;
}

QString playernames::draw(const avatars::Wearer& bot, language::Language language, const std::vector<Name>& names,
                          const QStringList& taken, int seatNumber, std::mt19937& random)
{
    // The free names dedicated to `strategy` (empty: to none) that suit the bot.
    const auto free = [&](const std::string& strategy)
    {
        QStringList found;

        for(const Name& name : names)
            if(name.strategy == strategy && suits(name, bot.gender, language) && !isTaken(name.name, taken))
                found.push_back(name.name);

        return found;
    };

    // 1. Its strategy's own; 2. anybody's.
    QStringList candidates = bot.strategy.empty() ? QStringList() : free(bot.strategy);

    if(candidates.isEmpty())
        candidates = free(std::string());

    if(!candidates.isEmpty())
    {
        std::uniform_int_distribution<qsizetype> pick(0, candidates.size() - 1);
        return candidates.at(pick(random));
    }

    // 3. RUN OUT: a placeholder, which the dialog's own fallback has always been.
    // From the seat's number up, so it is never one already at the table.
    for(int number = std::max(1, seatNumber) ; ; number++)
    {
        const QString placeholder = QCoreApplication::translate("NewGameDialog", "Bot %1").arg(number);

        if(!isTaken(placeholder, taken))
            return placeholder;
    }
}
