#include "DropdownMenu.h"

#include "paint/Fonts.h"
#include "paint/Shadows.h"
#include "ui/MenuBar.h"

#include <QActionEvent>
#include <QApplication>
#include <QFontMetricsF>
#include <QKeySequence>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOptionMenuItem>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace
{

// QMenu lays its rows out through its style, so this is where the rows get the panel's
// sizes. Everything else - the keyboard, the mouse, the hints - is the application style's.
class DropdownStyle : public QProxyStyle
{
public:
    explicit DropdownStyle(const QString& base) : QProxyStyle(base) {}

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        // The menu's contents margins place the rows, and nothing else does.
        switch(metric)
        {
        case PM_MenuPanelWidth:
        case PM_MenuHMargin:
        case PM_MenuVMargin:
        case PM_MenuDesktopFrameWidth:
            return 0;

        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size,
                           const QWidget* widget) const override
    {
        const DropdownMenu* menu = qobject_cast<const DropdownMenu*>(widget);
        const QStyleOptionMenuItem* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);

        if(type == CT_MenuItem && menu && item)
        {
            const bool separator = item->menuItemType == QStyleOptionMenuItem::Separator;
            return QSize(menu->rowWidth(), separator ? dropdown::separatorHeight : dropdown::rowHeight);
        }

        if(type == CT_Menu && menu)
            return size;

        return QProxyStyle::sizeFromContents(type, option, size, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returned) const override
    {
        switch(hint)
        {
        // A greyed row is never lit, and the menu keeps the shape the panel gives it.
        case SH_Menu_AllowActiveAndDisabled:
        case SH_Menu_Mask:
        case SH_Menu_FlashTriggeredItem:
        case SH_Menu_FadeOutOnHide:
            return 0;

        default:
            return QProxyStyle::styleHint(hint, option, widget, returned);
        }
    }

    // A system style dresses its menus with an opacity, a shadow or a palette of its own.
    void polish(QWidget* widget) override
    {
        if(!qobject_cast<DropdownMenu*>(widget))
            QProxyStyle::polish(widget);
    }

    void unpolish(QWidget* widget) override
    {
        if(!qobject_cast<DropdownMenu*>(widget))
            QProxyStyle::unpolish(widget);
    }

    using QProxyStyle::polish;
    using QProxyStyle::unpolish;
};

QString shortcutOf(const QAction* action)
{
    return action->shortcut().toString(QKeySequence::NativeText);
}

int insideOfPanel()
{
    return dropdown::borderWidth + dropdown::padding;
}

}

QMargins dropdown::shadowMargins()
{
    const int reach = static_cast<int>(std::ceil(shadows::reach(menubar::shadowBlur)));
    const int below = static_cast<int>(std::ceil(menubar::shadowOffset + shadows::reach(menubar::shadowBlur)));

    return QMargins(reach, drop, reach, below);
}

QFont dropdown::shortcutFont()
{
    QFont font = fonts::body();
    font.setPixelSize(shortcutPixels);
    return font;
}

DropdownMenu::DropdownMenu(const QString& title, QWidget* parent) : QMenu(title, parent)
{
    // Translucent for the rounded corners and the shadow, which is ours, not the system's.
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // A child, so it goes with the menu: a widget does not own its style.
    DropdownStyle* rows = new DropdownStyle(QApplication::style()->name());
    rows->setParent(this);
    setStyle(rows);

    const int inside = insideOfPanel();
    setContentsMargins(dropdown::shadowMargins() + QMargins(inside, inside, inside, inside));
}

void DropdownMenu::setTheme(const Theme& _theme)
{
    theme = _theme;
    framePicture = QPixmap();
    update();
}

void DropdownMenu::addItem(QAction* action, std::optional<icons::Icon> icon)
{
    addAction(action);

    if(icon)
        rowIcons.insert(action, *icon);
}

std::optional<icons::Icon> DropdownMenu::iconOf(const QAction* action) const
{
    const auto found = rowIcons.constFind(action);

    if(found == rowIcons.constEnd())
        return std::nullopt;

    return *found;
}

QRect DropdownMenu::panelRect() const
{
    return rect().marginsRemoved(dropdown::shadowMargins());
}

QRect DropdownMenu::rowRect(const QAction* action) const
{
    const QRect place = actionGeometry(const_cast<QAction*>(action));

    if(place.isNull())
        return QRect();

    const int inside = insideOfPanel();
    const QRect rows = panelRect().adjusted(inside, inside, -inside, -inside);

    return QRect(rows.left(), place.top(), rows.width(), place.height());
}

int DropdownMenu::rowWidth() const
{
    const QFontMetricsF labels(menubar::labelFont());
    const QFontMetricsF shortcuts(dropdown::shortcutFont());

    qreal widest = dropdown::minimumRowWidth;

    for(const QAction* action : actions())
    {
        if(action->isSeparator() || !action->isVisible())
            continue;

        qreal width = 2.0 * dropdown::rowPadding + dropdown::iconSize + dropdown::iconGap
                      + labels.horizontalAdvance(menubar::labelOf(action->text()));

        const QString shortcut = shortcutOf(action);

        if(!shortcut.isEmpty())
            width += dropdown::shortcutGap + shortcuts.horizontalAdvance(shortcut);

        widest = std::max(widest, width);
    }

    return static_cast<int>(std::ceil(widest));
}

QSize DropdownMenu::sizeHint() const
{
    // QMenu's own width adds a shortcut column measured in the system font, so the width
    // is the rows' and only the height is QMenu's.
    int bottom = contentsMargins().top();

    for(QAction* action : actions())
    {
        const QRect place = actionGeometry(action);

        if(!place.isNull())
            bottom = std::max(bottom, place.bottom() + 1);
    }

    const QMargins margins = contentsMargins();

    return QSize(margins.left() + rowWidth() + margins.right(), bottom + margins.bottom());
}

bool DropdownMenu::isTranslucent() const
{
    const QWindow* window = windowHandle();
    return !window || window->format().hasAlpha();
}

void DropdownMenu::updateMask()
{
    if(isTranslucent())
    {
        clearMask();
        return;
    }

    QPainterPath shape;
    shape.addRoundedRect(QRectF(panelRect()), dropdown::radius, dropdown::radius);
    setMask(QRegion(shape.toFillPolygon().toPolygon()));
}

const QPixmap& DropdownMenu::frame()
{
    const qreal ratio = devicePixelRatioF();
    const QSize pixels(static_cast<int>(std::ceil(width() * ratio)),
                       static_cast<int>(std::ceil(height() * ratio)));

    if(!framePicture.isNull() && framePicture.size() == pixels && framePicture.devicePixelRatio() == ratio)
        return framePicture;

    framePicture = QPixmap(pixels);
    framePicture.setDevicePixelRatio(ratio);
    framePicture.fill(Qt::transparent);

    const Theme::TablePalette& palette = theme.table;

    QPainter painter(&framePicture);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF panel(panelRect());

    QPainterPath outer;
    outer.addRoundedRect(panel, dropdown::radius, dropdown::radius);

    if(isTranslucent())
        shadows::drawShapeShadow(painter, outer, QPointF(0.0, menubar::shadowOffset), menubar::shadowBlur,
                                 palette.menuShadow, shadows::Side::Beneath);

    // The border as the outer shape filled and the inside laid over it, so the hairline
    // stays one pixel wide round the corners.
    painter.fillPath(outer, palette.panelBorder);

    const qreal border = dropdown::borderWidth;
    const QRectF inside = panel.adjusted(border, border, -border, -border);

    QPainterPath inner;
    inner.addRoundedRect(inside, dropdown::radius - border, dropdown::radius - border);

    QLinearGradient fall(0.0, inside.top(), 0.0, inside.bottom());
    fall.setColorAt(0.0, palette.menuTop);
    fall.setColorAt(1.0, palette.menuBottom);
    painter.fillPath(inner, fall);

    return framePicture;
}

void DropdownMenu::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, frame());

    painter.setRenderHint(QPainter::Antialiasing);

    for(const QAction* action : actions())
        paintRow(painter, action);
}

void DropdownMenu::paintRow(QPainter& painter, const QAction* action)
{
    const QRect row = rowRect(action);

    if(row.isEmpty() || !action->isVisible())
        return;

    const Theme::TablePalette& palette = theme.table;

    if(action->isSeparator())
    {
        const qreal middle = row.top() + std::floor(row.height() / 2.0);
        painter.fillRect(QRectF(row.left() + dropdown::rowPadding, middle,
                                row.width() - 2.0 * dropdown::rowPadding, 1.0),
                         palette.hudRule);
        return;
    }

    const bool enabled = action->isEnabled();
    const bool lit = enabled && action == activeAction();

    if(lit)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.menuHover);
        painter.drawRoundedRect(QRectF(row), menubar::itemRadius, menubar::itemRadius);
    }

    const menubar::Colours colours = menubar::coloursFor(palette, enabled, lit);

    const QRectF iconBox(row.left() + dropdown::rowPadding, row.top() + (row.height() - dropdown::iconSize) / 2.0,
                         dropdown::iconSize, dropdown::iconSize);

    if(const std::optional<icons::Icon> icon = iconOf(action))
    {
        icons::paint(painter, *icon, iconBox, colours.icon);
    }
    else if(action->isCheckable() && action->isChecked())
    {
        // The icons' 24-unit grid.
        const auto at = [&iconBox](qreal x, qreal y)
        {
            return QPointF(iconBox.left() + x * iconBox.width() / 24.0, iconBox.top() + y * iconBox.height() / 24.0);
        };

        QPainterPath tick(at(4.5, 12.5));
        tick.lineTo(at(9.5, 17.5));
        tick.lineTo(at(19.5, 6.5));

        painter.setPen(QPen(colours.icon, dropdown::tickWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(tick);
    }

    const QFont labelFont = menubar::labelFont();
    const QFontMetricsF labels(labelFont);
    const qreal labelLine = labels.lineSpacing();

    painter.setFont(labelFont);
    painter.setPen(colours.label);
    painter.drawText(QPointF(iconBox.right() + dropdown::iconGap,
                             fonts::baselineIn(labels, row.top() + (row.height() - labelLine) / 2.0, labelLine)),
                     menubar::labelOf(action->text()));

    const QString shortcut = shortcutOf(action);

    if(shortcut.isEmpty())
        return;

    const QFont shortcutFont = dropdown::shortcutFont();
    const QFontMetricsF shortcuts(shortcutFont);
    const qreal shortcutLine = shortcuts.lineSpacing();

    painter.setFont(shortcutFont);
    painter.setPen(lit ? palette.goldBright : palette.muted);
    painter.drawText(QPointF(row.left() + row.width() - dropdown::rowPadding - shortcuts.horizontalAdvance(shortcut),
                             fonts::baselineIn(shortcuts, row.top() + (row.height() - shortcutLine) / 2.0, shortcutLine)),
                     shortcut);
}

void DropdownMenu::mousePressEvent(QMouseEvent* event)
{
    // The shadow is outside the menu to the player, so a press on it closes the menu as a
    // press anywhere else outside does.
    const QPoint at = event->position().toPoint();

    if(rect().contains(at) && !panelRect().contains(at))
    {
        hide();
        return;
    }

    QMenu::mousePressEvent(event);
}

void DropdownMenu::mouseMoveEvent(QMouseEvent* event)
{
    // QMenu's rows reach past the panel by its shortcut column, so a row is not lit from
    // the shadow beside it.
    const QPoint at = event->position().toPoint();

    if(rect().contains(at) && !panelRect().contains(at))
    {
        setActiveAction(nullptr);
        return;
    }

    QMenu::mouseMoveEvent(event);
}

void DropdownMenu::showEvent(QShowEvent* event)
{
    QMenu::showEvent(event);

    framePicture = QPixmap();
    updateMask();
}

void DropdownMenu::resizeEvent(QResizeEvent* event)
{
    QMenu::resizeEvent(event);
    updateMask();
}

void DropdownMenu::actionEvent(QActionEvent* event)
{
    QMenu::actionEvent(event);

    if(event->type() == QEvent::ActionRemoved)
        rowIcons.remove(event->action());
}
