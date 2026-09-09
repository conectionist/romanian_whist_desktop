#ifndef MAIN_MENU_PAGE_H
#define MAIN_MENU_PAGE_H

#include "paint/Theme.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QPushButton;
QT_END_NAMESPACE

// The screen the client opens on, and the one it returns to when a game is
// abandoned. New Game and Quit, and nothing else yet - section 12's Game menu,
// as a page rather than as a menu.
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
class MainMenuPage : public QWidget
{
    Q_OBJECT

public:
    explicit MainMenuPage(QWidget* parent = nullptr);

    // The one palette the window owns, handed down like every other widget's -
    // no colour is constructed outside Theme (section 18.1).
    void setTheme(const Theme& theme);

signals:
    void newGameRequested();
    void quitRequested();

protected:
    // The table is drawn by TableScene onto a QGraphicsView, which paints its own
    // ground; this page has no scene, so it paints Theme's surface itself rather
    // than sitting on the window's default grey beside a green table.
    void paintEvent(QPaintEvent* event) override;

private:
    Theme theme = Theme::light();

    // The title and buttons live on this rather than straight on the painted
    // background - see the constructor for the legibility finding that put it
    // there.
    QWidget* panel = nullptr;

    QPushButton* newGame = nullptr;
    QPushButton* quit = nullptr;
};

#endif
