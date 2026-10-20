#include "Fonts.h"

#include <QDir>
#include <QFontDatabase>
#include <QString>
#include <QStringList>
#include <QtLogging>

#include <cmath>

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

namespace
{

QFont chain(const QStringList& families, QFont::StyleHint generic)
{
    QFont font;
    font.setFamilies(families);
    font.setStyleHint(generic);
    return font;
}

}

QFont logo()
{
    return chain({ QStringLiteral("Cinzel Decorative"), QStringLiteral("Cinzel"),
                   QStringLiteral("Georgia") },
                 QFont::Serif);
}

QFont heading()
{
    return chain({ QStringLiteral("Marcellus"), QStringLiteral("Cinzel"),
                   QStringLiteral("Georgia") },
                 QFont::Serif);
}

Sized sized(QFont font, qreal pixels)
{
    // Tenths are the finest the mock-up writes, so ten is as far as this looks.
    constexpr int finest = 10;

    for(int multiple = 1 ; multiple < finest ; multiple++)
    {
        const qreal whole = pixels * multiple;

        if(std::abs(whole - std::round(whole)) < 1e-6)
        {
            font.setPixelSize(static_cast<int>(std::round(whole)));
            return { font, static_cast<qreal>(multiple) };
        }
    }

    font.setPixelSize(static_cast<int>(std::round(pixels * finest)));
    return { font, static_cast<qreal>(finest) };
}

QFont body()
{
    return chain({ QStringLiteral("Alegreya Sans"), QStringLiteral("Gill Sans"),
                   QStringLiteral("Segoe UI") },
                 QFont::SansSerif);
}

} // namespace fonts
