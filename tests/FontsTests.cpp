#include <catch2/catch_test_macros.hpp>

#include "paint/Fonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
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

// The chains are what every later phase will draw its text in, so a typo in a
// bundled family's name there would be text in the first fallback on every
// machine - and the registration test above would still pass. Resolving each
// chain is the test of the chain itself.
TEST_CASE("Each fallback chain resolves to its bundled family", "[fonts]")
{
    for(const QFont& font : { fonts::logo(), fonts::heading(), fonts::body() })
    {
        const QStringList chain = font.families();

        INFO(chain.join(QStringLiteral(", ")).toStdString());

        REQUIRE(QFontInfo(font).family() == chain.first());
    }
}

// THE GENERIC LAST STEP, which is a style hint and not a name (Fonts.h has why).
// Nothing can make every named face go missing to watch the hint take over, so
// this asserts the hint is there and that no chain still ends in a CSS keyword.
TEST_CASE("Each fallback chain ends in a generic family Qt understands", "[fonts]")
{
    REQUIRE(fonts::logo().styleHint() == QFont::Serif);
    REQUIRE(fonts::heading().styleHint() == QFont::Serif);
    REQUIRE(fonts::body().styleHint() == QFont::SansSerif);

    for(const QFont& font : { fonts::logo(), fonts::heading(), fonts::body() })
    {
        REQUIRE_FALSE(font.families().contains(QStringLiteral("serif")));
        REQUIRE_FALSE(font.families().contains(QStringLiteral("sans-serif")));
    }
}

// BUNDLING A FACE MUST NOT CHANGE THE DEFAULT ONE. In the offscreen test platform
// on Windows it DID: a default family that is missing gets filled alphabetically,
// and "Alegreya Sans" sorts in front of every system face (TestApp.cpp has the
// detail). The first sign of it was text wrapping differently in a test about
// the bid strip. Asserted here, so the next time it happens the failure names
// its cause.
//
// What that proves depends on where it runs. On Linux and macOS, that the
// registration had no side effect. On Windows offscreen, that TestApp's pin
// RESOLVES - this asks QFontInfo, so a pinned face missing from the machine
// would fall to the tie-break and fail here. On Windows with the windows
// platform plugin, that the system's own UI font is untouched.
//
// By PREFIX, because the Medium file also carries a legacy family name, "Alegreya
// Sans Medium", and a default resolved to that is the same failure.
TEST_CASE("The default font is none of the bundled typefaces", "[fonts]")
{
    const QString family = QFontInfo(QGuiApplication::font()).family();

    INFO("default font: " << family.toStdString());

    for(const QFont& font : { fonts::logo(), fonts::heading(), fonts::body() })
        REQUIRE_FALSE(family.startsWith(font.families().first()));
}
