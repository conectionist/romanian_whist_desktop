#ifndef DROPDOWN_MENU_H
#define DROPDOWN_MENU_H

#include "paint/Icons.h"
#include "paint/Theme.h"

#include <QFont>
#include <QHash>
#include <QMargins>
#include <QMenu>
#include <QPixmap>

#include <optional>

// One of the menu bar's menus: a real QMenu, so the keyboard, the mouse and closing come
// from Qt, painted in the bar's colours rather than the system's.
//
// In device-independent pixels, as the bar is.
namespace dropdown
{

// The panel's top is this far below the bar, its left edge under its button's.
inline constexpr int drop = 4;

// The bar's gradient inside a 1 px border in the bar's border colour, rounded 8, the rows
// 6 in from the border.
inline constexpr int borderWidth = 1;
inline constexpr qreal radius = 8.0;
inline constexpr int padding = 6;

// A row is as tall as a bar item and lit as one is: an 18 px icon column 12 in, the label
// 8 after it, and the shortcut at the right, at least 32 after its label.
inline constexpr int rowHeight = 34;
inline constexpr int rowPadding = 12;
inline constexpr int iconSize = 18;
inline constexpr int iconGap = 8;
inline constexpr int shortcutGap = 32;
inline constexpr int minimumRowWidth = 180;
inline constexpr int shortcutPixels = 14;

// A separator: a 1 px line in the middle of its 9.
inline constexpr int separatorHeight = 9;

// The tick on a checked row, in pixels, so it stays bold at the icons' size.
inline constexpr qreal tickWidth = 2.0;

// Room round the panel for the bar's shadow, 0 6px 16px. Above the panel only the gap to
// the bar: a popup reaching higher would cover the bar's buttons and take their mouse.
QMargins shadowMargins();

// The shortcut's font: Alegreya Sans 14, through section 2.3's fallback chain.
QFont shortcutFont();

} // namespace dropdown

class DropdownMenu : public QMenu
{
    Q_OBJECT

public:
    explicit DropdownMenu(const QString& title, QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // Adds `action` as a row, with `icon` in its icon column.
    void addItem(QAction* action, std::optional<icons::Icon> icon = std::nullopt);

    std::optional<icons::Icon> iconOf(const QAction* action) const;

    // The panel's place in the popup, which leaves room round it for the shadow.
    QRect panelRect() const;

    // Where `action`'s row is drawn and takes the mouse: its place in the menu, across
    // the panel's inside.
    QRect rowRect(const QAction* action) const;

    // How wide every row is: the widest row's contents, or the minimum.
    int rowWidth() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void actionEvent(QActionEvent* event) override;

private:
    // Whether the window system shows the popup's transparent pixels as transparent.
    // Where it does not, the popup is cut to the panel's shape and has no shadow.
    bool isTranslucent() const;
    void updateMask();

    // The shadow and the panel without its rows, kept until the size, ratio or theme
    // changes: the shadow's blur is the costly part, and every hover repaints.
    const QPixmap& frame();

    void paintRow(QPainter& painter, const QAction* action);

    Theme theme = Theme::light();
    QHash<const QAction*, icons::Icon> rowIcons;
    QPixmap framePicture;
};

#endif
