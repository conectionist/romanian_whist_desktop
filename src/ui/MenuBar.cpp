#include "MenuBar.h"

#include "paint/Fonts.h"
#include "paint/Shadows.h"
#include "ui/DropdownMenu.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

int menubar::shadowHeight()
{
    return static_cast<int>(std::ceil(shadowOffset + shadows::reach(shadowBlur)));
}

qreal menubar::shadowAt(qreal y)
{
    // Across a straight edge a Gaussian blur of a step is the error function (see
    // shadows::drawCircleShadow()). The bar's box, moved down by the offset, ends
    // `shadowOffset` below the bar, so that is where the shadow is at half.
    const qreal sigma = shadows::sigma(shadowBlur);

    return 0.5 * std::erfc((y - shadowOffset) / (sigma * std::sqrt(2.0)));
}

QFont menubar::labelFont()
{
    QFont font = fonts::heading();
    font.setPixelSize(labelPixels);
    return font;
}

QString menubar::labelOf(const QString& text)
{
    QString label;
    label.reserve(text.size());

    for(qsizetype i = 0 ; i < text.size() ; i++)
    {
        if(text.at(i) == QLatin1Char('&'))
        {
            // "&&" is one '&'; a lone one marks the next character and goes.
            if(i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&'))
            {
                label += QLatin1Char('&');
                i++;
            }

            continue;
        }

        label += text.at(i);
    }

    return label;
}

QString menubar::withoutMnemonic(const QString& text)
{
    QString plain;
    plain.reserve(text.size());

    for(qsizetype i = 0 ; i < text.size() ; i++)
    {
        if(text.at(i) != QLatin1Char('&'))
        {
            plain += text.at(i);
            continue;
        }

        // "&&" stays as it is; a lone '&' goes.
        if(i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&'))
        {
            plain += QStringLiteral("&&");
            i++;
        }
    }

    return plain;
}

menubar::Colours menubar::coloursFor(const Theme::TablePalette& palette, bool enabled, bool lit)
{
    if(!enabled)
        return { palette.muted, palette.muted };

    if(lit)
        return { palette.goldBright, palette.goldBright };

    return { palette.gold, palette.cream };
}

MenuBarButton::MenuBarButton(QWidget* parent) : QToolButton(parent)
{
    // Repainted as the cursor comes and goes, which is all the hover look needs.
    setAttribute(Qt::WA_Hover);

    // Tab only, never a click: a click must not take the keyboard from the table, and left
    // on a bar button, Space would press it again.
    setFocusPolicy(Qt::TabFocus);

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // page.html's .btn `cursor: pointer`. A disabled widget takes its parent's cursor, so
    // a greyed item shows the plain arrow.
    setCursor(Qt::PointingHandCursor);
}

void MenuBarButton::setTheme(const Theme& _theme)
{
    theme = _theme;
    update();
}

bool MenuBarButton::isLit() const
{
    // While a menu is open only its own button is lit: the open menu takes the mouse, so
    // underMouse() keeps whatever it was when the menu opened.
    if(QApplication::activePopupWidget())
        return isDown();

    // .btn:hover and :focus-visible look the same, and focus comes only by Tab, which is
    // focus-visible. Down is the button holding its menu open.
    return underMouse() || hasFocus() || isDown();
}

QString MenuBarButton::label() const
{
    return menubar::labelOf(text());
}

QSize MenuBarButton::sizeHint() const
{
    const QFontMetricsF metrics(menubar::labelFont());

    const qreal width = 2.0 * menubar::itemPadding + metrics.horizontalAdvance(label());

    return QSize(static_cast<int>(std::ceil(width)), menubar::itemHeight);
}

QSize MenuBarButton::minimumSizeHint() const
{
    return sizeHint();
}

void MenuBarButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const Theme::TablePalette& palette = theme.table;
    const bool lit = isEnabled() && isLit();

    if(lit)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.menuHover);
        painter.drawRoundedRect(QRectF(rect()), menubar::itemRadius, menubar::itemRadius);
    }

    const menubar::Colours colours = menubar::coloursFor(palette, isEnabled(), lit);

    // align-items: center: the label's line box is centred on the item's height.
    const QFont font = menubar::labelFont();
    const QFontMetricsF metrics(font);
    const qreal line = metrics.lineSpacing();

    painter.setFont(font);
    painter.setPen(colours.label);
    painter.drawText(QPointF(menubar::itemPadding, fonts::baselineIn(metrics, (height() - line) / 2.0, line)),
                     label());
}

MenuBarShadow::MenuBarShadow(QWidget* parent) : QWidget(parent)
{
    // Drawn over the page, not part of it: every click and hover goes to whatever
    // lies beneath, the scoreboard's top edge or a sheet's.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
}

void MenuBarShadow::setTheme(const Theme& _theme)
{
    theme = _theme;
    update();
}

void MenuBarShadow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    const Theme::TablePalette& palette = theme.table;

    // 0 6px 16px rgba(0, 0, 0, 0.45): the profile sampled into a gradient, two
    // dozen stops over a shadow 30 deep, close enough that no step shows.
    constexpr int stops = 24;

    const qreal depth = menubar::shadowHeight();
    QLinearGradient fall(0.0, 0.0, 0.0, depth);

    for(int i = 0 ; i <= stops ; i++)
    {
        const qreal at = static_cast<qreal>(i) / stops;

        QColor sample = palette.menuShadow;
        sample.setAlphaF(palette.menuShadow.alphaF() * menubar::shadowAt(at * depth));
        fall.setColorAt(at, sample);
    }

    painter.fillRect(rect(), fall);

    // 0 1px 0 rgba(201, 164, 92, 0.22): the bar moved down one pixel, so a line
    // one pixel deep along its edge. Listed first, so CSS draws it over the other.
    painter.fillRect(QRect(0, 0, width(), 1), palette.menuGlint);
}

MenuBar::MenuBar(const Items& given, QWidget* window) : QWidget(window)
{
    setFixedHeight(menubar::height);

    // The shadow is the window's child, so it can lie over the page beneath the
    // bar; placeShadow() keeps it there.
    shade = new MenuBarShadow(window);

    QHBoxLayout* row = new QHBoxLayout(this);

    // The border is the bar's own bottom pixel, so the items centre in the 45
    // above it. Qt centres in whole pixels: 5 above each item, where CSS has 5.5.
    row->setContentsMargins(menubar::sidePadding, 0, menubar::sidePadding, menubar::borderWidth);
    row->setSpacing(menubar::itemGap);

    addMenu(given.file, QStringLiteral("menuFile"));
    addMenu(given.game, QStringLiteral("menuGame"));
    addMenu(given.view, QStringLiteral("menuView"));

    for(MenuBarButton* button : items)
        row->addWidget(button);

    row->addStretch(1);
}

MenuBar::~MenuBar()
{
    delete shade.data();
}

void MenuBar::addMenu(DropdownMenu* menu, const QString& name)
{
    const std::size_t index = items.size();

    MenuBarButton* button = new MenuBarButton(this);
    button->setObjectName(name);

    // In the form an action's text reaches its button: label() reads it the same way,
    // and it carries no mnemonic to claim the window's Alt shortcut (see withoutMnemonic()).
    button->setText(menu ? menubar::withoutMnemonic(menu->title()) : QString());

    items.push_back(button);
    menus.push_back(menu);

    if(!menu)
        return;

    // On the press, as a menu bar opens its menus, so a press, a drag and a release on a
    // row choose it.
    connect(button, &QAbstractButton::pressed, this, [this, index]{ open(index, false); });

    connect(menu, &QMenu::aboutToHide, this, [this, button]
    {
        button->setDown(false);

        // Once the switch to another menu, if this is one, has opened it.
        QTimer::singleShot(0, this, [this]
        {
            if(!openMenu())
                emit menuClosed();
        });
    });

    menu->installEventFilter(this);
}

void MenuBar::setTheme(const Theme& _theme)
{
    theme = _theme;

    for(MenuBarButton* button : items)
        button->setTheme(theme);

    for(DropdownMenu* menu : menus)
    {
        if(menu)
            menu->setTheme(theme);
    }

    if(shade)
        shade->setTheme(theme);

    update();
}

MenuBarShadow* MenuBar::shadow() const
{
    return shade.data();
}

const std::vector<MenuBarButton*>& MenuBar::buttons() const
{
    return items;
}

MenuBarButton* MenuBar::button(Menu which) const
{
    return items.at(static_cast<std::size_t>(which));
}

DropdownMenu* MenuBar::menu(Menu which) const
{
    return menus.at(static_cast<std::size_t>(which));
}

DropdownMenu* MenuBar::openMenu() const
{
    for(DropdownMenu* menu : menus)
    {
        if(menu && menu->isVisible())
            return menu;
    }

    return nullptr;
}

void MenuBar::showMenu(Menu which)
{
    open(static_cast<std::size_t>(which), true);
}

QPoint MenuBar::menuPosition(Menu which) const
{
    const MenuBarButton* under = button(which);
    const QMargins margins = dropdown::shadowMargins();

    const QPoint panel = mapToGlobal(QPoint(under->x(), height() + dropdown::drop));

    return panel - QPoint(margins.left(), margins.top());
}

void MenuBar::open(std::size_t index, bool fromKeyboard)
{
    DropdownMenu* menu = menus.at(index);

    if(!menu || !isEnabled())
        return;

    for(DropdownMenu* other : menus)
    {
        if(other && other != menu && other->isVisible())
            other->hide();
    }

    // A press on the open menu's own button closes it and must not be replayed to the
    // button, which would open it again; the filter below sets this for that press.
    menu->setAttribute(Qt::WA_NoMouseReplay, false);

    items.at(index)->setDown(true);
    menu->popup(menuPosition(static_cast<Menu>(index)));

    if(!fromKeyboard)
        return;

    const QList<QAction*> actions = menu->actions();
    const auto first = std::find_if(actions.begin(), actions.end(), [](const QAction* action)
    {
        return action->isVisible() && action->isEnabled() && !action->isSeparator();
    });

    if(first != actions.end())
        menu->setActiveAction(*first);
}

bool MenuBar::eventFilter(QObject* watched, QEvent* event)
{
    const auto found = std::find(menus.begin(), menus.end(), watched);

    if(found == menus.end())
        return QWidget::eventFilter(watched, event);

    const std::size_t index = static_cast<std::size_t>(found - menus.begin());

    // Left and Right go to the menu beside, round from one end to the other.
    if(event->type() == QEvent::KeyPress)
    {
        const int key = static_cast<QKeyEvent*>(event)->key();

        if(key == Qt::Key_Left || key == Qt::Key_Right)
        {
            const std::size_t count = menus.size();
            open(key == Qt::Key_Right ? (index + 1) % count : (index + count - 1) % count, true);
            return true;
        }
    }

    // The open menu has the mouse, so it is here that the cursor reaching another button
    // is seen, and a press on this menu's own button.
    if(event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)
    {
        const QPoint global = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();

        for(std::size_t i = 0 ; i < items.size() ; i++)
        {
            MenuBarButton* button = items.at(i);

            if(!button->isVisible() || !button->rect().contains(button->mapFromGlobal(global)))
                continue;

            if(event->type() == QEvent::MouseButtonPress)
            {
                if(i == index)
                    menus.at(index)->setAttribute(Qt::WA_NoMouseReplay);
            }
            else if(i != index && button->isEnabled())
            {
                open(i, false);
                return true;
            }

            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MenuBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    const Theme::TablePalette& palette = theme.table;
    const int inside = height() - menubar::borderWidth;

    // linear-gradient(180deg, #2e1c10, #1a1009), sized to the padding box - the
    // bar above its border.
    QLinearGradient fall(0.0, 0.0, 0.0, inside);
    fall.setColorAt(0.0, palette.menuTop);
    fall.setColorAt(1.0, palette.menuBottom);

    painter.fillRect(QRect(0, 0, width(), inside), fall);
    painter.fillRect(QRect(0, inside, width(), menubar::borderWidth), palette.panelBorder);
}

void MenuBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeShadow();
}

void MenuBar::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    placeShadow();
}

void MenuBar::placeShadow()
{
    if(!shade)
        return;

    shade->setGeometry(x(), y() + height(), width(), menubar::shadowHeight());

    // Over the central widget, which the window may have been given after this.
    shade->raise();
}
