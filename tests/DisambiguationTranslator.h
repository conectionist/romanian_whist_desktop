#ifndef DISAMBIGUATION_TRANSLATOR_H
#define DISAMBIGUATION_TRANSLATOR_H

#include <QCoreApplication>
#include <QString>
#include <QTranslator>

// A TRANSLATOR THAT SAYS WHICH VARIANT WAS ASKED FOR. A string translated with a
// disambiguation comes back as "[tag] source", so a test can see which of
// persontext::'s variants a sentence picked without a .qm file. Anything
// translated without one is left alone and shows its English source.
//
// THE TAG IS THE DISAMBIGUATION CUT SHORT - "about you, a woman" is "you woman" -
// because the trick panel keeps every line to one, shortening the name to fit:
// the whole comment pushed "First to bid: You" past its column.
//
// INSTALLED FOR ITS OWN LIFETIME ONLY, so it cannot outlive its test case and
// turn the next one's text (Catch2 runs cases in a random order).
class DisambiguationTranslator : public QTranslator
{
public:
    DisambiguationTranslator()
    {
        QCoreApplication::installTranslator(this);
    }

    ~DisambiguationTranslator() override
    {
        QCoreApplication::removeTranslator(this);
    }

    DisambiguationTranslator(const DisambiguationTranslator&) = delete;
    DisambiguationTranslator& operator=(const DisambiguationTranslator&) = delete;

    QString translate(const char*, const char* sourceText, const char* disambiguation, int) const override
    {
        if(disambiguation == nullptr || *disambiguation == '\0')
            return QString();

        QString tag = QString::fromUtf8(disambiguation);
        tag.remove(QStringLiteral("about ")).remove(QStringLiteral("a ")).remove(QLatin1Char(','));

        return QStringLiteral("[%1] %2").arg(tag, QString::fromUtf8(sourceText));
    }

    // Not empty, or Qt may treat it as a translator with nothing in it.
    bool isEmpty() const override
    {
        return false;
    }
};

#endif
