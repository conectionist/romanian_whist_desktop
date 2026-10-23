#include <catch2/catch_test_macros.hpp>

#include "paint/Fonts.h"
#include "table/TableButtonItem.h"
#include "ui/LanguageDialog.h"
#include "ui/MainMenuPage.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPushButton>

#include <cmath>

// THE LANGUAGE THE CLIENT RUNS IN (the redesign's phase 13, the owner's call of
// 2026-10-23): --language en|ro on the command line, or the dialog that asks
// before the main window is built.
using language::Language;

TEST_CASE("A language code reads as the command line gives it", "[language]")
{
    REQUIRE(language::fromCode(QStringLiteral("en")) == Language::English);
    REQUIRE(language::fromCode(QStringLiteral("ro")) == Language::Romanian);

    // Any case, a country after it either way, and space round it.
    REQUIRE(language::fromCode(QStringLiteral("RO")) == Language::Romanian);
    REQUIRE(language::fromCode(QStringLiteral("ro_RO")) == Language::Romanian);
    REQUIRE(language::fromCode(QStringLiteral("ro-RO")) == Language::Romanian);
    REQUIRE(language::fromCode(QStringLiteral("en_GB")) == Language::English);
    REQUIRE(language::fromCode(QStringLiteral(" en ")) == Language::English);

    // A language the client does not speak, and no language at all.
    REQUIRE_FALSE(language::fromCode(QStringLiteral("de")));
    REQUIRE_FALSE(language::fromCode(QStringLiteral("xx")));
    REQUIRE_FALSE(language::fromCode(QStringLiteral("romanian-ish")));
    REQUIRE_FALSE(language::fromCode(QString()));
}

TEST_CASE("The dialog starts on the first of the system's languages the client speaks", "[language]")
{
    REQUIRE(language::fromSystem({ QStringLiteral("ro-RO"), QStringLiteral("en-US") }) == Language::Romanian);
    REQUIRE(language::fromSystem({ QStringLiteral("en-US"), QStringLiteral("ro-RO") }) == Language::English);

    // Past the ones it does not speak.
    REQUIRE(language::fromSystem({ QStringLiteral("de-DE"), QStringLiteral("ro") }) == Language::Romanian);

    // And English when there are none it speaks.
    REQUIRE(language::fromSystem({ QStringLiteral("de-DE"), QStringLiteral("fr") }) == Language::English);
    REQUIRE(language::fromSystem({}) == Language::English);
}

TEST_CASE("Each language has its code its own name and its translation", "[language]")
{
    REQUIRE(language::codeOf(Language::English) == QStringLiteral("en"));
    REQUIRE(language::codeOf(Language::Romanian) == QStringLiteral("ro"));

    // A code reads back as its language.
    for(const Language each : { Language::English, Language::Romanian })
        REQUIRE(language::fromCode(language::codeOf(each)) == each);

    REQUIRE(language::nativeName(Language::English) == QStringLiteral("English"));
    REQUIRE(language::nativeName(Language::Romanian) == QStringLiteral("Română"));

    // English is the source language: nothing to load.
    REQUIRE(language::translationResource(Language::English).isEmpty());
    REQUIRE(language::translationResource(Language::Romanian) == QStringLiteral(":/i18n/romanian_whist_desktop_ro_RO"));
}

TEST_CASE("A button without an icon is its padding and its label", "[language]")
{
    QFont label = fonts::heading();
    label.setPixelSize(tablebutton::labelSize);

    const QString text = QStringLiteral("Română");
    const qreal advance = QFontMetricsF(label).horizontalAdvance(text);

    // A border of 1 and 22 of padding either side, and no icon or the gap after it.
    REQUIRE(std::abs(tablebutton::sizeFor(text, false).width() - (46.0 + advance)) < 1e-9);
    REQUIRE(tablebutton::sizeFor(text, true).width() - tablebutton::sizeFor(text, false).width() == 36.0);

    MenuPageButton bare(std::nullopt, text);
    MenuPageButton iconed(icons::Icon::NewGame, text);

    REQUIRE(bare.sizeHint().width() < iconed.sizeHint().width());
}

TEST_CASE("The language dialog offers both languages and starts on the one it was given", "[language]")
{
    for(const Language preselected : { Language::English, Language::Romanian })
    {
        LanguageDialog dialog(preselected);

        QPushButton* english = dialog.findChild<QPushButton*>(QStringLiteral("en"));
        QPushButton* romanian = dialog.findChild<QPushButton*>(QStringLiteral("ro"));

        REQUIRE(english != nullptr);
        REQUIRE(romanian != nullptr);
        REQUIRE(english->text() == QStringLiteral("English"));
        REQUIRE(romanian->text() == QStringLiteral("Română"));

        // The preselected one is Enter's answer, and the choice until another is made.
        QPushButton* first = preselected == Language::Romanian ? romanian : english;
        QPushButton* other = preselected == Language::Romanian ? english : romanian;

        REQUIRE(first->isDefault());
        REQUIRE_FALSE(other->isDefault());
        REQUIRE(dialog.choice() == preselected);

        // Side by side, the same size, inside the dialog.
        REQUIRE(english->size() == romanian->size());
        REQUIRE(english->geometry().top() == romanian->geometry().top());
        REQUIRE(english->geometry().right() < romanian->geometry().left());
        REQUIRE(dialog.rect().contains(english->geometry()));
        REQUIRE(dialog.rect().contains(romanian->geometry()));
    }
}

TEST_CASE("Choosing a language answers the dialog with it", "[language]")
{
    LanguageDialog dialog(Language::English);

    // A count rather than QSignalSpy, which lives in Qt6::Test.
    int accepted = 0;
    QObject::connect(&dialog, &QDialog::accepted, [&accepted] { accepted++; });

    dialog.findChild<QPushButton*>(QStringLiteral("ro"))->click();

    REQUIRE(accepted == 1);
    REQUIRE(dialog.result() == QDialog::Accepted);
    REQUIRE(dialog.choice() == Language::Romanian);
}

TEST_CASE("Enter takes the preselected language and Esc answers nothing", "[language]")
{
    {
        LanguageDialog dialog(Language::Romanian);
        dialog.show();
        QCoreApplication::processEvents();

        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(&dialog, &enter);

        REQUIRE(dialog.result() == QDialog::Accepted);
        REQUIRE(dialog.choice() == Language::Romanian);
    }

    {
        LanguageDialog dialog(Language::English);
        dialog.show();
        QCoreApplication::processEvents();

        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&dialog, &escape);

        REQUIRE(dialog.result() == QDialog::Rejected);
        REQUIRE_FALSE(dialog.isVisible());
    }
}
