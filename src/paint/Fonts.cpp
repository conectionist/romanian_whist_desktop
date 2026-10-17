#include "Fonts.h"

#include <QDir>
#include <QFontDatabase>
#include <QString>
#include <QtLogging>

namespace fonts
{

// THE .qrc IS THE LIST. Enumerating the resource directory rather than naming
// the five files here means adding a weight is one line in fonts.qrc, and
// there is no second list to forget.
void registerBundled()
{
    const QDir directory(QStringLiteral(":/fonts"));
    const QStringList files = directory.entryList({ QStringLiteral("*.ttf") }, QDir::Files);

    // An empty directory is the one failure the per-file warning below cannot
    // report, because there is no file to name.
    if(files.isEmpty())
    {
        qWarning("fonts: no typefaces under :/fonts - is fonts.qrc compiled into this binary?");
        return;
    }

    for(const QString& file : files)
    {
        const QString path = directory.filePath(file);

        if(QFontDatabase::addApplicationFont(path) < 0)
            qWarning("fonts: could not load %s", qUtf8Printable(path));
    }
}

QStringList logo()
{
    return { QStringLiteral("Cinzel Decorative"), QStringLiteral("Cinzel"),
             QStringLiteral("Georgia"), QStringLiteral("serif") };
}

QStringList heading()
{
    return { QStringLiteral("Marcellus"), QStringLiteral("Cinzel"),
             QStringLiteral("Georgia"), QStringLiteral("serif") };
}

QStringList body()
{
    return { QStringLiteral("Alegreya Sans"), QStringLiteral("Gill Sans"),
             QStringLiteral("Segoe UI"), QStringLiteral("sans-serif") };
}

} // namespace fonts
