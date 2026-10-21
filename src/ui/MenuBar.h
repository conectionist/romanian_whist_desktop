#ifndef MENU_BAR_H
#define MENU_BAR_H

#include "paint/Icons.h"
#include "paint/Theme.h"

#include <QColor>
#include <QFont>
#include <QPointer>
#include <QToolButton>
#include <QWidget>

#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
QT_END_NAMESPACE

// THE MOCK-UP'S MENU BAR, section 2.12 of docs/ui-redesign/IMPLEMENTATION_PLAN.md:
// page.html's .menubar, added in the redesign's phase 9.
//
// A WIDGET OF OUR OWN, installed with QMainWindow::setMenuWidget(), and not a
// styled QMenuBar, which is what the plan first asked for. QMenuBar draws an
// item's icon OR its text, never both (QCommonStyle's CE_MenuBarItem), so no
// stylesheet puts the mock-up's gold icon beside its label; and it has no stretch,
// so Exit cannot sit at the far right. Each item is still a QAction, so what a
// menu action gives - triggering, enabling, a shortcut - works as it did.
//
// IN DEVICE-INDEPENDENT PIXELS, unlike everything on the table: the bar does not
// scale with the table, in the mock-up or here.
namespace menubar
{

// page.html's numbers. The bar is 46 tall with its 1 px border included
// (box-sizing: border-box), 16 in from each side, its items 4 apart and centred
// in the 45 above the border.
inline constexpr int height = 46;
inline constexpr int borderWidth = 1;
inline constexpr int sidePadding = 16;
inline constexpr int itemGap = 4;

// .menubar .btn: 34 tall, padding 0 12, an 18 px icon 8 before its label, a
// rounded background under the cursor; Marcellus 17.
inline constexpr int itemHeight = 34;
inline constexpr int itemPadding = 12;
inline constexpr int iconSize = 18;
inline constexpr int iconGap = 8;
inline constexpr qreal itemRadius = 6.0;
inline constexpr int labelPixels = 17;

// .players' margin-right, which View takes over with the Players switch's place:
// View ends this far, plus an item gap, before Exit.
inline constexpr int viewMargin = 14;

// The bar's box-shadow, 0 6px 16px: CSS's offset and blur radius.
inline constexpr qreal shadowOffset = 6.0;
inline constexpr qreal shadowBlur = 16.0;

// How far below the bar its shadow reaches, in whole pixels.
int shadowHeight();

// What fraction of menuShadow's alpha the shadow has at `y` below the bar's
// bottom edge: the error function across the edge of the bar's box, moved down
// by its offset. About 0.77 at the edge itself and nothing at shadowHeight().
qreal shadowAt(qreal y);

// The items' label font: Marcellus 17, through section 2.3's fallback chain.
QFont labelFont();

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

// One item: an icon, if it has one, and a label, from the QAction it stands for
// (QToolButton::setDefaultAction()), or from a menu it opens (View).
class MenuBarButton : public QToolButton
{
    Q_OBJECT

public:
    explicit MenuBarButton(std::optional<icons::Icon> icon, QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // Under the cursor, focused from the keyboard, or holding its menu open.
    bool isLit() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<icons::Icon> icon;
    Theme theme = Theme::light();
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
    // What the bar holds, left to right: New Game, Settings and Rules, then at the
    // far right View (the developer's Log and Card art, where the mock-up's
    // Players switch sits) and Exit.
    struct Items
    {
        QAction* newGame = nullptr;
        QAction* settings = nullptr;
        QAction* rules = nullptr;
        QMenu* view = nullptr;
        QAction* exit = nullptr;
    };

    // `window` is what the bar goes into: its shadow is made a child of it.
    MenuBar(const Items& items, QWidget* window);
    ~MenuBar() override;

    void setTheme(const Theme& theme);

    // For tests.
    MenuBarShadow* shadow() const;
    const std::vector<MenuBarButton*>& buttons() const;

    // Opens the View menu: Alt+V's target, since a button that pops a menu up on
    // a press does nothing for the mnemonic's click.
    void showViewMenu();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    // Lays the shadow along the bar's bottom edge and raises it.
    void placeShadow();

    MenuBarButton* addItem(QAction* action, icons::Icon icon, const QString& name);

    Theme theme = Theme::light();

    std::vector<MenuBarButton*> items;
    MenuBarButton* viewButton = nullptr;

    // A sibling, not a child, so the bar does not own it: guarded, and deleted
    // with the bar if the window has not already taken it down.
    QPointer<MenuBarShadow> shade;
};

#endif
