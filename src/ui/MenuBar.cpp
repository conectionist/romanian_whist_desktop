#include "MenuBar.h"

#include "paint/Fonts.h"
#include "paint/Shadows.h"

#include <QAction>
#include <QBoxLayout>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>

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

MenuBarButton::MenuBarButton(std::optional<icons::Icon> _icon, QWidget* parent)
    : QToolButton(parent), icon(_icon)
{
    // Repainted as the cursor comes and goes, which is all the hover look needs.
    setAttribute(Qt::WA_Hover);

    // TAB ONLY, never a click: the bid strip holds the keyboard for its digit keys
    // while you bid, and a click on New Game or Rules must not take it away.
    setFocusPolicy(Qt::TabFocus);

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void MenuBarButton::setTheme(const Theme& _theme)
{
    theme = _theme;
    update();
}

bool MenuBarButton::isLit() const
{
    // .menubar .btn:hover and :focus-visible look the same. Focus reaches this only
    // by Tab, so having it is being focus-visible. Down is View with its menu open.
    return underMouse() || hasFocus() || isDown();
}

QString MenuBarButton::label() const
{
    return menubar::labelOf(text());
}

QSize MenuBarButton::sizeHint() const
{
    const QFontMetricsF metrics(menubar::labelFont());

    qreal width = 2.0 * menubar::itemPadding + metrics.horizontalAdvance(label());

    if(icon)
        width += menubar::iconSize + menubar::iconGap;

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

    qreal left = menubar::itemPadding;

    // align-items: center, so the icon and the label's line box are both centred
    // on the item's height, as on the corner buttons.
    if(icon)
    {
        icons::paint(painter, *icon,
                     QRectF(left, (height() - menubar::iconSize) / 2.0,
                            menubar::iconSize, menubar::iconSize),
                     colours.icon);

        left += menubar::iconSize + menubar::iconGap;
    }

    const QFont font = menubar::labelFont();
    const QFontMetricsF metrics(font);
    const qreal line = metrics.lineSpacing();

    painter.setFont(font);
    painter.setPen(colours.label);
    painter.drawText(QPointF(left, fonts::baselineIn(metrics, (height() - line) / 2.0, line)),
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

    row->addWidget(addItem(given.newGame, icons::Icon::NewGame, QStringLiteral("menuNewGame")));
    row->addWidget(addItem(given.settings, icons::Icon::Settings, QStringLiteral("menuSettings")));
    row->addWidget(addItem(given.rules, icons::Icon::Rules, QStringLiteral("menuRules")));

    // margin-left: auto on the Players switch, which View stands in for.
    row->addStretch(1);

    viewButton = new MenuBarButton(std::nullopt, this);
    viewButton->setObjectName(QStringLiteral("menuView"));
    // In the form an action's text reaches its button: label() reads it the same
    // way, and it carries no mnemonic to claim Alt+V (see withoutMnemonic()).
    viewButton->setText(given.view ? menubar::withoutMnemonic(given.view->title()) : QString());
    viewButton->setMenu(given.view);
    viewButton->setPopupMode(QToolButton::InstantPopup);
    items.push_back(viewButton);
    row->addWidget(viewButton);

    // A spacer adds only its own width - the layout's spacing goes between the
    // items either side of it - so this and the gap make the margin and the gap
    // the mock-up has there.
    row->addSpacing(menubar::viewMargin);

    row->addWidget(addItem(given.exit, icons::Icon::Exit, QStringLiteral("menuExit")));
}

MenuBar::~MenuBar()
{
    delete shade.data();
}

MenuBarButton* MenuBar::addItem(QAction* action, icons::Icon icon, const QString& name)
{
    MenuBarButton* button = new MenuBarButton(icon, this);
    button->setObjectName(name);
    button->setDefaultAction(action);
    items.push_back(button);
    return button;
}

void MenuBar::setTheme(const Theme& _theme)
{
    theme = _theme;

    for(MenuBarButton* button : items)
        button->setTheme(theme);

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

void MenuBar::showViewMenu()
{
    if(viewButton->menu() && isEnabled())
        viewButton->showMenu();
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
