#ifndef MAIN_MENU_PAGE_H
#define MAIN_MENU_PAGE_H

#include "paint/Icons.h"
#include "paint/Panel.h"
#include "paint/Theme.h"
#include "ui/LitButton.h"

#include <QPixmap>
#include <QSize>
#include <QWidget>

#include <optional>
#include <vector>

// One of the menu page's two buttons: section 2.11's .panel.btn - a gold line icon
// and a heading-face label on the table's panel - in device-independent pixels, as
// the sheets are.
//
// IT PAINTS ITS FACE AND NOTHING ELSE. The panel behind it has a drop shadow that
// reaches past the button's rect, which a widget cannot paint into, so the page
// paints the panel and this the icon and label on it (tablebutton::paintFace(),
// the corner buttons' own).
//
// A QPushButton, so it keeps what a button does - the click, Enter and Space, the
// default - and the name every test finds it by. Lit by a sheet button's rule
// (LitButton::isLit()).
class MenuPageButton : public LitButton
{
    Q_OBJECT

public:
    // No icon gives the label alone, centred: the language dialog's buttons.
    MenuPageButton(std::optional<icons::Icon> icon, const QString& text, QWidget* parent = nullptr);

    void setColours(const Theme::TablePalette& palette);

    // .panel.btn's size for its label: 76 tall, as wide as its icon and label
    // with 22 either side (tablebutton::sizeFor()).
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<icons::Icon> icon;
    Theme::TablePalette colours;
};

// The screen the client opens on, and the one it returns to when a game is
// abandoned. New Game and Quit, and nothing else yet - section 12's Game menu,
// as a page rather than as a menu.
//
// IN THE TABLE'S LOOK SINCE THE REDESIGN'S PHASE 10: the room the table stands in,
// the table's own logo plaque for a title, and the two buttons in the corner
// buttons' panel. It was the last page on the old green and light palette.
//
// A DEPARTURE FROM SECTION 12, and a deliberate one. That section puts New Game,
// Abandon Game and Quit in a thin menu bar; this client has a menu page and a
// pause menu instead, because "abandon" with no way back is not what a player
// wants mid-game and Resume is. What is unchanged is what the buttons DO:
// abandoning, quitting, closing the window and starting a second game are the
// same three calls in the same order, which is the whole reason section 6
// insists stop() be idempotent.
//
// THE TERMINAL OPENS THE SAME WAY. TerminalRomanianWhist::startGame() runs the
// setup wizard before a card exists, so there is no game at launch there either
// - which is what "the app never needs a command line" (section 8, phase D)
// actually means.
//
// A PAGE AND NOT A DIALOG, so that no game exists while it is up. The window's
// central widget is a QStackedWidget and this is page 0; the table is page 1 and
// keeps its own state between games. Nothing here has to tolerate a half-torn-
// down game, because a game is stopped and joined before this is shown.
namespace menupage
{

// The logo plaque is drawn at twice its size on the table: the page's title.
inline constexpr qreal logoScale = 2.0;

// Between the plaque and the first button, and between the two buttons - the
// corner buttons' own gap (tablespace::buttonGap).
inline constexpr int logoGap = 40;
inline constexpr int buttonGap = 14;

} // namespace menupage

class MainMenuPage : public QWidget
{
    Q_OBJECT

public:
    explicit MainMenuPage(QWidget* parent = nullptr);

    // The one palette the window owns, handed down like every other widget's -
    // no colour is constructed outside Theme (section 18.1).
    void setTheme(const Theme& theme);

    // Puts the keyboard on New Game, so Enter deals - where it goes back to when
    // a sheet over this page closes.
    void focusNewGame();

    // Where the logo plaque is drawn - the plaque itself, without its shadow: READ
    // by the tests.
    QRect logoRect() const;

signals:
    void newGameRequested();
    void quitRequested();

protected:
    // The room, the logo, and the buttons' panels under their faces.
    void paintEvent(QPaintEvent* event) override;

    // Lays the logo and the buttons out, centred on the page.
    void resizeEvent(QResizeEvent* event) override;

    // The buttons' cursor and keyboard, which light the panels this paints.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void layOut();

    // The logo at `ratio`, painted once per ratio and palette.
    const QPixmap& logo(qreal ratio) const;

    Theme theme = Theme::dark();

    MenuPageButton* newGame = nullptr;
    MenuPageButton* quit = nullptr;
    std::vector<MenuPageButton*> buttons;

    // Where the logo's centre is.
    QPointF logoCentre;

    mutable QPixmap logoPicture;
    mutable Theme::TablePalette logoPalette;

    // The buttons' panels, plain and lit, with their shadows.
    mutable panel::Cache chrome;
};

#endif
