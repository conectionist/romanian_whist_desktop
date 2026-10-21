#include <catch2/catch_test_macros.hpp>

#include "paint/Theme.h"
#include "ui/MainMenuPage.h"

#include <QCoreApplication>
#include <QImage>
#include <QPushButton>

#include <cstdlib>

// The page the client opens on, in the table's look since the redesign's phase 10:
// the room, the logo plaque for a title, and New Game and Quit as the corner
// buttons' panels. Where things are has a right answer; how they look is checked
// by eye in menu.png.
namespace
{

// What the page paints, and nothing behind it.
QImage ownPixels(QWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image, QPoint(), QRegion(), QWidget::RenderFlags());
    return image;
}

bool near(const QColor& a, const QColor& b, int tolerance)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
        && std::abs(a.blue() - b.blue()) <= tolerance;
}

}

TEST_CASE("The menu page is the room with the logo over its two buttons", "[menu-page]")
{
    MainMenuPage page;
    page.setTheme(Theme::dark());
    page.resize(1280, 800);
    page.show();
    QCoreApplication::processEvents();

    QPushButton* newGame = page.findChild<QPushButton*>("newGame");
    QPushButton* quit = page.findChild<QPushButton*>("quit");
    REQUIRE(newGame != nullptr);
    REQUIRE(quit != nullptr);

    // A column, the buttons as wide as each other, under the plaque.
    REQUIRE(newGame->width() == quit->width());
    REQUIRE(newGame->x() == quit->x());
    REQUIRE(page.logoRect().bottom() < newGame->y());
    REQUIRE(newGame->geometry().bottom() < quit->y());

    // All of it centred across the page.
    REQUIRE(std::abs(newGame->geometry().center().x() - page.rect().center().x()) <= 1);
    REQUIRE(std::abs(page.logoRect().center().x() - page.rect().center().x()) <= 1);

    const QImage image = ownPixels(page);
    const Theme::TablePalette palette = Theme::dark().table;

    // THE ROOM, section 2.2's gradient, dark to its corners: not the old green.
    const QColor corner = image.pixelColor(2, 2);
    REQUIRE(corner.lightness() < 30);
    REQUIRE(corner.red() >= corner.green());

    // THE LOGO, the table's plaque: its green, between its fine inner line and its
    // left-hand club.
    const QRect logo = page.logoRect();
    const QColor plaque = image.pixelColor(logo.left() + 22, logo.center().y());
    REQUIRE(plaque.green() > plaque.red());

    // And the buttons' panels behind their faces: the gold border along the
    // middle of New Game's top edge.
    const QColor border = image.pixelColor(newGame->geometry().center().x(), newGame->y());
    REQUIRE(near(border, palette.panelBorder, 12));
}

TEST_CASE("A menu page button lights under the cursor and when Tab brings the keyboard", "[menu-page]")
{
    MainMenuPage page;
    page.resize(1280, 800);
    page.show();
    QCoreApplication::processEvents();

    MenuPageButton* newGame = page.findChild<MenuPageButton*>("newGame");
    REQUIRE(newGame != nullptr);
    REQUIRE_FALSE(newGame->underMouse());

    // Where a closed sheet puts the keyboard, which lights nothing.
    page.focusNewGame();
    REQUIRE_FALSE(newGame->isLit());

    newGame->clearFocus();
    newGame->setFocus(Qt::TabFocusReason);
    REQUIRE(newGame->isLit());

    newGame->clearFocus();
    REQUIRE_FALSE(newGame->isLit());

    newGame->setAttribute(Qt::WA_UnderMouse, true);
    REQUIRE(newGame->isLit());

    // Lit, the page draws its panel lit: the inner line brightens.
    const QImage lit = ownPixels(page);

    newGame->setAttribute(Qt::WA_UnderMouse, false);
    const QImage plain = ownPixels(page);

    const QPoint line(newGame->geometry().center().x(), newGame->y() + 4);
    REQUIRE(lit.pixelColor(line) != plain.pixelColor(line));
}
