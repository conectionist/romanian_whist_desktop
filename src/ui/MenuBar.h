#ifndef MENU_BAR_H
#define MENU_BAR_H

#include "paint/Theme.h"

#include <QColor>
#include <QFont>
#include <QMargins>
#include <QPointer>
#include <QToolButton>
#include <QWidget>

#include <vector>

class DropdownMenu;

// The mock-up's menu bar, page.html's .menubar, with File, Game and View at the left. A
// widget of our own, installed with QMainWindow::setMenuWidget(), so it is laid out and
// painted to the mock-up's numbers rather than a style's.
//
// In device-independent pixels, unlike everything on the table: the bar does not scale
// with the table, in the mock-up or here.
namespace menubar
{

// page.html's numbers. The bar is 46 tall with its 1 px border included
// (box-sizing: border-box), 16 in from each side, its items 4 apart and centred
// in the 45 above the border.
inline constexpr int height = 46;
inline constexpr int borderWidth = 1;
inline constexpr int sidePadding = 16;
inline constexpr int itemGap = 4;

// .menubar .btn: 34 tall, padding 0 12, a rounded background under the cursor;
// Marcellus 17.
inline constexpr int itemHeight = 34;
inline constexpr int itemPadding = 12;
inline constexpr qreal itemRadius = 6.0;
inline constexpr int labelPixels = 17;

// The bar's box-shadow, 0 6px 16px: CSS's offset and blur radius.
inline constexpr qreal shadowOffset = 6.0;
inline constexpr qreal shadowBlur = 16.0;

// How far below the bar its shadow reaches, in whole pixels.
int shadowHeight();

// What fraction of menuShadow's alpha the shadow has at `y` below the bar's
// bottom edge: the error function across the edge of the bar's box, moved down
// by its offset. About 0.77 at the edge itself and nothing at shadowHeight().
qreal shadowAt(qreal y);

// The items' label font: Belleza 17 (page.html's Marcellus 17), through section 2.3's
// fallback chain.
QFont labelFont();

// What an item shows for a Qt text: a '&' marks a mnemonic and is not shown,
// and "&&" is one literal '&'. A painted label has no underline to show a
// mnemonic with, so the mark is dropped and nothing else (PR #35's review,
// finding #15).
QString labelOf(const QString& text);

// The same text with its mnemonic marks dropped but "&&" kept - the form an
// action's iconText() hands a button. A button must never be given a text with
// a mnemonic in it: QAbstractButton::setText() registers the mnemonic as a
// shortcut of its own, and View's Alt+V would then be claimed twice - by the
// button and by the window's QShortcut - so that neither fires.
QString withoutMnemonic(const QString& text);

// What colour an item's icon and label take. .btn: the icon gold and the label
// cream; both gold-bright under the cursor or the keyboard ("lit"); and, as the
// corner buttons are, both muted when the item can't be used.
struct Colours
{
    QColor icon;
    QColor label;
};

Colours coloursFor(const Theme::TablePalette& palette, bool enabled, bool lit);

} // namespace menubar

// One item: the label of the menu it opens.
class MenuBarButton : public QToolButton
{
    Q_OBJECT

public:
    explicit MenuBarButton(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // Under the cursor, focused from the keyboard, or holding its menu open.
    bool isLit() const;

    // What is drawn, and sized for: text(), mnemonics read as Qt reads them.
    QString label() const;

    // Whether the last press came from the keyboard rather than the mouse.
    bool pressedByKeyboard() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    Theme theme = Theme::light();
    bool keyboardPress = false;
};

// The bar's shadow, on whatever page lies under the bar. The mock-up's bar is
// z-index 10 over the whole stage, so its shadow falls across the table, the
// corner panels and any sheet raised over them; a widget can paint only inside
// itself, so this is a sibling of the bar, laid under it and raised over the
// window's central widget. The mouse passes through it.
class MenuBarShadow : public QWidget
{
    Q_OBJECT

public:
    explicit MenuBarShadow(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Theme theme = Theme::light();
};

class MenuBar : public QWidget
{
    Q_OBJECT

public:
    // The bar's menus, left to right.
    enum class Menu
    {
        File,
        Game,
        View
    };

    struct Items
    {
        DropdownMenu* file = nullptr;
        DropdownMenu* game = nullptr;
        DropdownMenu* view = nullptr;
    };

    // `window` is what the bar goes into: its shadow is made a child of it.
    MenuBar(const Items& items, QWidget* window);
    ~MenuBar() override;

    void setTheme(const Theme& theme);

    // For tests.
    MenuBarShadow* shadow() const;
    const std::vector<MenuBarButton*>& buttons() const;
    MenuBarButton* button(Menu which) const;
    DropdownMenu* menu(Menu which) const;

    // The menu that is open, if one is.
    DropdownMenu* openMenu() const;

    // Opens a menu as the keyboard does, its first item chosen: the target of Alt+F, Alt+G
    // and Alt+V, which a button opening its menu on a press would not answer.
    void showMenu(Menu which);

    // Where a menu is popped up, in global coordinates: its panel under its button.
    QPoint menuPosition(Menu which) const;

    // How much of its shadow a menu has room for there before a screen's edge.
    QMargins shadowRoom(Menu which) const;

signals:
    // A menu closed and no other took its place. `keyboardOnBar`: it was opened by the
    // keyboard from a bar button that has the keyboard, and keeps it.
    void menuClosed(bool keyboardOnBar);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Lays the shadow along the bar's bottom edge and raises it.
    void placeShadow();

    void addMenu(DropdownMenu* menu, const QString& name);

    // The panel's top-left corner, in global coordinates.
    QPoint panelCorner(Menu which) const;

    // Opens the menu at `index`, closing any other. From the keyboard, its first item is
    // chosen, as a QMenuBar's is.
    void open(std::size_t index, bool fromKeyboard);

    Theme theme = Theme::light();

    std::vector<MenuBarButton*> items;
    std::vector<DropdownMenu*> menus;
    bool keyboardOnBar = false;

    // A sibling, not a child, so the bar does not own it: guarded, and deleted
    // with the bar if the window has not already taken it down.
    QPointer<MenuBarShadow> shade;
};

#endif
