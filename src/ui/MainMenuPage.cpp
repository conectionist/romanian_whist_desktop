#include "MainMenuPage.h"

#include "paint/DevicePixels.h"
#include "table/TableButtonItem.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"

#include <QEvent>
#include <QFocusEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

// ---- MenuPageButton ----

MenuPageButton::MenuPageButton(icons::Icon _icon, const QString& text, QWidget* parent)
    : QPushButton(text, parent), icon(_icon), colours(Theme::dark().table)
{
    // Repainted as the cursor comes and goes, which is all the lit face needs; the
    // page repaints the panel behind it (MainMenuPage::eventFilter()).
    setAttribute(Qt::WA_Hover);
}

void MenuPageButton::setColours(const Theme::TablePalette& palette)
{
    colours = palette;
    update();
}

bool MenuPageButton::isLit() const
{
    return isEnabled() && (underMouse() || isDown() || (tabbedTo && hasFocus()));
}

QSize MenuPageButton::sizeHint() const
{
    const QSizeF size = tablebutton::sizeFor(text());

    return QSize(static_cast<int>(std::ceil(size.width())), static_cast<int>(std::ceil(size.height())));
}

QSize MenuPageButton::minimumSizeHint() const
{
    return sizeHint();
}

void MenuPageButton::focusInEvent(QFocusEvent* event)
{
    tabbedTo = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    update();

    QPushButton::focusInEvent(event);
}

void MenuPageButton::focusOutEvent(QFocusEvent* event)
{
    tabbedTo = false;
    update();

    QPushButton::focusOutEvent(event);
}

void MenuPageButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    tablebutton::paintFace(painter, QRectF(rect()), icon, text(),
                           tablebutton::coloursFor(colours, isEnabled(), isLit()));
}

// ---- MainMenuPage ----

MainMenuPage::MainMenuPage(QWidget* parent) : QWidget(parent), logoPalette(theme.table)
{
    newGame = new MenuPageButton(icons::Icon::NewGame, tr("New Game"), this);
    newGame->setObjectName("newGame");
    newGame->setDefault(true);

    quit = new MenuPageButton(icons::Icon::Exit, tr("Quit"), this);
    quit->setObjectName("quit");

    buttons = { newGame, quit };

    for(MenuPageButton* button : buttons)
        button->installEventFilter(this);

    connect(newGame, &QPushButton::clicked, this, &MainMenuPage::newGameRequested);
    connect(quit, &QPushButton::clicked, this, &MainMenuPage::quitRequested);

    layOut();
}

void MainMenuPage::setTheme(const Theme& _theme)
{
    theme = _theme;

    for(MenuPageButton* button : buttons)
        button->setColours(theme.table);

    update();
}

void MainMenuPage::focusNewGame()
{
    newGame->setFocus(Qt::OtherFocusReason);
}

QRect MainMenuPage::logoRect() const
{
    // The plaque without its shadow: its frame at the table's 0.9, and that at
    // this page's own scale.
    const QSizeF plaque = tablespace::logoBox * (tablespace::logoScale * menupage::logoScale);

    return QRectF(logoCentre - QPointF(plaque.width() / 2.0, plaque.height() / 2.0), plaque).toRect();
}

void MainMenuPage::layOut()
{
    // A FIXED COLUMN, not buttons stretched to the window: the page is as wide as
    // the table it replaces, and a New Game button a metre across reads as a
    // banner rather than as something to press. Both as wide as the wider.
    int across = 0;

    for(MenuPageButton* button : buttons)
        across = std::max(across, button->sizeHint().width());

    const int down = newGame->sizeHint().height();
    const qreal plaqueHeight = logoRect().height();

    // The plaque, the gap, and the two buttons, centred on the page as one.
    const qreal total = plaqueHeight + menupage::logoGap + 2.0 * down + menupage::buttonGap;
    const qreal top = std::max(0.0, (height() - total) / 2.0);

    logoCentre = QPointF(width() / 2.0, top + plaqueHeight / 2.0);

    const int left = (width() - across) / 2;
    const int first = static_cast<int>(std::round(top + plaqueHeight + menupage::logoGap));

    newGame->setGeometry(left, first, across, down);
    quit->setGeometry(left, first + down + menupage::buttonGap, across, down);

    update();
}

const QPixmap& MainMenuPage::logo(qreal ratio) const
{
    if(!logoPicture.isNull() && logoPicture.devicePixelRatio() == ratio && logoPalette == theme.table)
        return logoPicture;

    // THE TABLE'S OWN PLAQUE (tabletop::paintLogo()), scaled up as a whole, and
    // painted once per ratio: its lettering and blurred shadow are too dear for
    // every repaint of a button over it.
    const QRectF bounds = tabletop::logoBounds();
    const qreal scale = menupage::logoScale;

    QPixmap picture(QSize(static_cast<int>(std::ceil(bounds.width() * scale * ratio)),
                          static_cast<int>(std::ceil(bounds.height() * scale * ratio))));
    picture.setDevicePixelRatio(ratio);
    picture.fill(Qt::transparent);

    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(scale, scale);
    painter.translate(-bounds.topLeft());

    tabletop::paintLogo(painter, theme, QPointF(0.0, 0.0));
    painter.end();

    logoPicture = picture;
    logoPalette = theme.table;

    return logoPicture;
}

void MainMenuPage::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal ratio = devicePixelRatioF();

    // THE ROOM THE TABLE STANDS IN, through Theme's own function - so the menu and
    // the game are one place.
    painter.drawPixmap(0, 0, theme.room(size(), ratio));

    const QRectF bounds = tabletop::logoBounds();
    const QPointF corner = logoCentre + bounds.topLeft() * menupage::logoScale;

    painter.drawPixmap(devicepixels::snapped(painter, corner), logo(ratio));

    // The buttons' panels, lit as the buttons are, under the faces they paint.
    for(MenuPageButton* button : buttons)
    {
        const panel::State state = button->isLit() ? panel::State::Hovered : panel::State::Plain;
        const panel::Picture made = chrome.picture(QSizeF(button->size()), theme.table, state, ratio);

        if(!made.pixmap.isNull())
        {
            painter.drawPixmap(devicepixels::snapped(painter, QPointF(button->pos()) + made.offset),
                               made.pixmap);
        }
    }

    QWidget::paintEvent(event);
}

void MainMenuPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layOut();
}

bool MainMenuPage::eventFilter(QObject* watched, QEvent* event)
{
    // WHAT LIGHTS A BUTTON lights the panel this paints behind it.
    switch(event->type())
    {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::EnabledChange:
        update();
        break;

    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}
