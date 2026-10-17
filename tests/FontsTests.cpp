#include <catch2/catch_test_macros.hpp>

#include "paint/Fonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QString>
#include <QStringList>

// The fonts are registered once for the whole binary, by TestApp.cpp, through
// the same fonts::registerBundled() main() calls - so these assert the state the
// application starts in, not one a test set up for itself.

// FONT EMBEDDING FAILS AS QUIETLY AS ANY RESOURCE: a missing face is not an
// error anywhere, only text in Georgia where Marcellus was meant. Asking the
// database is the one place the difference is a yes or a no.
TEST_CASE("The bundled typefaces are registered", "[fonts]")
{
    const QStringList families = QFontDatabase::families();

    REQUIRE(families.contains(QStringLiteral("Cinzel Decorative")));
    REQUIRE(families.contains(QStringLiteral("Marcellus")));
    REQUIRE(families.contains(QStringLiteral("Alegreya Sans")));

    // ALL THREE WEIGHTS RESOLVE TO REAL FACES, asked the way a widget will ask.
    // The redesign draws body text at 400, names in the panels at 500 and labels
    // at 700. A missing weight is not an error either: the font engine would
    // match the nearest face, or embolden the regular one, and a 500 drawn as
    // 400 looks only slightly off - so the weight the match reports is checked,
    // not only the family.
    //
    // The Medium file is the one with a second, legacy family name, "Alegreya
    // Sans Medium", beside the typographic "Alegreya Sans". If a platform's font
    // database ever files it under the legacy name, the Medium row here is what
    // fails.
    for(const QFont::Weight weight : { QFont::Normal, QFont::Medium, QFont::Bold })
    {
        QFont font(QStringLiteral("Alegreya Sans"));
        font.setWeight(weight);

        const QFontInfo resolved(font);

        INFO("weight " << weight);
        REQUIRE(resolved.family() == QStringLiteral("Alegreya Sans"));
        REQUIRE(resolved.weight() == weight);
    }
}

// The chains are what every later phase will hand to QFont::setFamilies(), so a
// typo in a bundled family's name there would be text in the first fallback on
// every machine - and the registration test above would still pass. Resolving
// each chain is the test of the chain itself.
TEST_CASE("Each fallback chain resolves to its bundled family", "[fonts]")
{
    for(const QStringList& chain : { fonts::logo(), fonts::heading(), fonts::body() })
    {
        INFO(chain.join(QStringLiteral(", ")).toStdString());

        QFont font;
        font.setFamilies(chain);

        REQUIRE(QFontInfo(font).family() == chain.first());
    }
}
