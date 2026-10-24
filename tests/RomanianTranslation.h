#ifndef ROMANIAN_TRANSLATION_H
#define ROMANIAN_TRANSLATION_H

#include "ui/LanguageDialog.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTranslator>

// THE APP'S OWN ROMANIAN, loaded as main() loads it (language::translationResource())
// from the copy tests/CMakeLists.txt compiles into this binary. For a test that
// checks a sentence really comes out in Romanian, which an English run cannot see.
//
// INSTALLED FOR ITS OWN LIFETIME ONLY, as DisambiguationTranslator is: every other
// case asserts English, and Catch2 runs cases in a random order.
class RomanianTranslation
{
public:
    RomanianTranslation()
    {
        REQUIRE(translator.load(language::translationResource(language::Language::Romanian)));

        installed = QCoreApplication::installTranslator(&translator);
        REQUIRE(installed);
    }

    ~RomanianTranslation()
    {
        if(installed)
            QCoreApplication::removeTranslator(&translator);
    }

    RomanianTranslation(const RomanianTranslation&) = delete;
    RomanianTranslation& operator=(const RomanianTranslation&) = delete;

private:
    QTranslator translator;
    bool installed = false;
};

#endif
