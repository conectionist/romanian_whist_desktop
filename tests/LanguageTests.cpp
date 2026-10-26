#include <catch2/catch_test_macros.hpp>

#include "ui/LanguageDialog.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>

#include <cstdlib>

// THE LANGUAGE THE CLIENT RUNS IN (the redesign's phase 13, the owner's call of
// 2026-10-23): --language en|ro on the command line, or the dialog that asks
// before the main window is built.
using language::Language;

namespace
{

bool near(const QColor& got, const QColor& want, int tolerance = 6)
{
    return std::abs(got.red() - want.red()) <= tolerance && std::abs(got.green() - want.green()) <= tolerance
        && std::abs(got.blue() - want.blue()) <= tolerance;
}

// `language`'s flag alone, 90 x 60: the dialog's 3:2.
QImage flagOf(Language language)
{
    QImage image(QSize(90, 60), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    language::paintFlag(painter, QRectF(0.0, 0.0, 90.0, 60.0), language);

    return image;
}

}

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

TEST_CASE("Romania's flag is blue yellow and red in three even bands", "[language]")
{
    const QImage flag = flagOf(Language::Romanian);

    REQUIRE(near(flag.pixelColor(15, 30), QColor(0x00, 0x2B, 0x7F)));
    REQUIRE(near(flag.pixelColor(45, 30), QColor(0xFC, 0xD1, 0x16)));
    REQUIRE(near(flag.pixelColor(75, 30), QColor(0xCE, 0x11, 0x26)));

    // Even: each band's colour runs to a third of the width, top to bottom.
    REQUIRE(near(flag.pixelColor(28, 5), QColor(0x00, 0x2B, 0x7F)));
    REQUIRE(near(flag.pixelColor(32, 55), QColor(0xFC, 0xD1, 0x16)));
}

TEST_CASE("The Union Flag has its blue field its white and red crosses", "[language]")
{
    // Read on its 60 x 30 grid, stretched 1.5 across and 2 down into 90 x 60.
    const QImage flag = flagOf(Language::English);
    const auto at = [&](qreal x, qreal y) { return flag.pixelColor(qRound(x * 1.5), qRound(y * 2.0)); };

    const QColor blue(0x01, 0x21, 0x69);
    const QColor red(0xC8, 0x10, 0x2E);

    // The middle of the red cross, and its white edge.
    REQUIRE(near(at(30.0, 15.0), red));
    REQUIRE(near(at(26.0, 4.0), Qt::white));

    // The blue field, clear of every stripe.
    REQUIRE(near(at(18.0, 3.0), blue));
    REQUIRE(near(at(45.0, 27.0), blue));

    // COUNTERCHANGED: in the upper hoist the red runs below the white diagonal's
    // middle line, and in the upper fly above it - so the same distance off the
    // diagonal is red on one side and white on the other.
    REQUIRE(near(at(10.0, 6.5), red));
    REQUIRE(near(at(10.0, 3.5), Qt::white));
    REQUIRE(near(at(50.0, 3.5), red));
    REQUIRE(near(at(50.0, 6.5), Qt::white));
}

TEST_CASE("The language dialog offers a flag per language with Romanian first and lit to start", "[language]")
{
    LanguageDialog dialog;

    QPushButton* romanian = dialog.findChild<QPushButton*>(QStringLiteral("ro"));
    QPushButton* english = dialog.findChild<QPushButton*>(QStringLiteral("en"));

    REQUIRE(romanian != nullptr);
    REQUIRE(english != nullptr);

    // The button's text is its language's name, for a screen reader and for the
    // name painted under it.
    REQUIRE(romanian->text() == QStringLiteral("Română"));
    REQUIRE(english->text() == QStringLiteral("English"));

    // Romanian is Enter's answer, and the choice until another is made (the owner's
    // call).
    REQUIRE(romanian->isDefault());
    REQUIRE_FALSE(english->isDefault());
    REQUIRE(dialog.choice() == Language::Romanian);

    // Side by side, Romanian on the left, the same size, inside the dialog.
    REQUIRE(romanian->size() == english->size());
    REQUIRE(romanian->geometry().top() == english->geometry().top());
    REQUIRE(romanian->geometry().right() < english->geometry().left());

    // Each name under its own button, and inside the dialog.
    for(const QPushButton* button : { romanian, english })
    {
        const QRectF name = dialog.nameRect(static_cast<const FlagButton*>(button));

        REQUIRE(name.top() > button->geometry().bottom());
        REQUIRE(name.left() == button->geometry().left());
        REQUIRE(name.width() == button->width());
        REQUIRE(QRectF(dialog.rect()).contains(name));
    }

    // And no words beside the two names: the flags are the question.
    REQUIRE(dialog.findChildren<QPushButton*>().size() == 2);
}

TEST_CASE("Choosing a language answers the dialog with it", "[language]")
{
    LanguageDialog dialog;

    // A count rather than QSignalSpy, which lives in Qt6::Test.
    int accepted = 0;
    QObject::connect(&dialog, &QDialog::accepted, [&accepted] { accepted++; });

    dialog.findChild<QPushButton*>(QStringLiteral("en"))->click();

    REQUIRE(accepted == 1);
    REQUIRE(dialog.result() == QDialog::Accepted);
    REQUIRE(dialog.choice() == Language::English);
}

TEST_CASE("Enter takes Romanian and Esc answers nothing", "[language]")
{
    {
        LanguageDialog dialog;
        dialog.show();
        QCoreApplication::processEvents();

        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(&dialog, &enter);

        REQUIRE(dialog.result() == QDialog::Accepted);
        REQUIRE(dialog.choice() == Language::Romanian);
    }

    {
        LanguageDialog dialog;
        dialog.show();
        QCoreApplication::processEvents();

        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&dialog, &escape);

        REQUIRE(dialog.result() == QDialog::Rejected);
        REQUIRE_FALSE(dialog.isVisible());
    }
}
