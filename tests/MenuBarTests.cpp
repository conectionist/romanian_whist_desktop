#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "RestingMouse.h"
#include "WindowDriving.h"
#include "paint/Theme.h"
#include "ui/DropdownMenu.h"
#include "ui/MenuBar.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPointingDevice>
#include <QScreen>
#include <QTest>
#include <QWidget>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>

// The menu bar and its menus on their own, in a bare window. The window's use of them is
// MainWindowTests'.
namespace
{
using driving::pump;

// A window holding a bar with the three menus MainWindow builds, and their rows.
struct Fixture
{
    QMainWindow window;

    QAction* newGame = new QAction(QStringLiteral("New Game"), &window);
    QAction* quit = new QAction(QStringLiteral("Quit"), &window);
    QAction* pause = new QAction(QStringLiteral("Pause"), &window);
    QAction* settings = new QAction(QStringLiteral("Settings"), &window);
    QAction* rules = new QAction(QStringLiteral("Rules"), &window);
    QAction* log = new QAction(QStringLiteral("&Log"), &window);
    QAction* cardArt = nullptr;

    DropdownMenu* file = new DropdownMenu(QStringLiteral("File"), &window);
    DropdownMenu* game = new DropdownMenu(QStringLiteral("Game"), &window);
    DropdownMenu* view = new DropdownMenu(QStringLiteral("View"), &window);

    MenuBar* bar = nullptr;

    explicit Fixture(const QString& fileTitle = QStringLiteral("File"))
    {
        file->setTitle(fileTitle);

        newGame->setShortcut(QKeySequence(Qt::ALT | Qt::Key_N));
        pause->setShortcut(QKeySequence(Qt::ALT | Qt::Key_X));
        settings->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
        rules->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
        log->setCheckable(true);

        file->addItem(newGame, icons::Icon::NewGame);
        file->addItem(quit, icons::Icon::Exit);

        game->addItem(pause, icons::Icon::Pause);
        game->addItem(settings, icons::Icon::Settings);
        game->addItem(rules, icons::Icon::Rules);

        view->addItem(log);
        view->addSeparator();
        cardArt = view->addAction(QStringLiteral("&Card art..."));

        bar = new MenuBar({ file, game, view }, &window);
        bar->setTheme(Theme::dark());
        window.setMenuWidget(bar);
        window.setCentralWidget(new QWidget(&window));

        window.resize(760, 600);
        window.show();
        pump(50);
    }

    ~Fixture()
    {
        // A popup left open takes the next case's mouse and keyboard.
        for(DropdownMenu* menu : { file, game, view })
            menu->hide();
    }

    MenuBarButton* button(const char* name) const
    {
        return bar->findChild<MenuBarButton*>(QString::fromLatin1(name));
    }
};

bool near(QColor a, QColor b, int tolerance)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

// `over` drawn on `under` at its alpha, as an opaque pixel shows it.
QColor over(QColor over, QColor under)
{
    const qreal a = over.alphaF();

    return QColor::fromRgbF(over.redF() * a + under.redF() * (1.0 - a), over.greenF() * a + under.greenF() * (1.0 - a),
                            over.blueF() * a + under.blueF() * (1.0 - a));
}

void press(QWidget* widget, QPoint at)
{
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(at), QPointF(widget->mapToGlobal(at)), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

void release(QWidget* widget, QPoint at)
{
    QMouseEvent event(QEvent::MouseButtonRelease, QPointF(at), QPointF(widget->mapToGlobal(at)), Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

// A move as the open menu sees it: wherever the cursor is, the popup has the mouse.
void moveOver(QWidget* menu, QPoint global)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(menu->mapFromGlobal(global)), QPointF(global), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(menu, &event);
}

void key(QWidget* widget, int code)
{
    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

QPoint middleOf(const QWidget* widget)
{
    return widget->mapToGlobal(widget->rect().center());
}

// The menu as it is drawn, in its own logical pixels.
QImage pictureOf(DropdownMenu* menu)
{
    QImage image = menu->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    image.setDevicePixelRatio(1.0);
    return image.scaled(menu->size());
}

// How many of `area`'s pixels are within `tolerance` of `colour`.
int pixelsNear(const QImage& image, const QRect& area, QColor colour, int tolerance)
{
    int count = 0;

    for(int y = area.top() ; y <= area.bottom() ; y++)
    {
        for(int x = area.left() ; x <= area.right() ; x++)
        {
            const QColor pixel = image.pixelColor(x, y);

            if(pixel.alpha() == 255 && near(pixel, colour, tolerance))
                count++;
        }
    }

    return count;
}

// The leftmost column in `area` with a pixel near `colour`, or -1.
int firstColumnNear(const QImage& image, const QRect& area, QColor colour, int tolerance)
{
    for(int x = area.left() ; x <= area.right() ; x++)
    {
        for(int y = area.top() ; y <= area.bottom() ; y++)
        {
            if(near(image.pixelColor(x, y), colour, tolerance))
                return x;
        }
    }

    return -1;
}

}

TEST_CASE("The menu bar is 46 high with File Game and View at the left", "[menu-bar]")
{
    Fixture fixture;

    REQUIRE(fixture.bar->height() == menubar::height);
    REQUIRE(fixture.bar->width() == fixture.window.width());

    const std::vector<MenuBarButton*>& buttons = fixture.bar->buttons();
    REQUIRE(buttons.size() == 3);

    REQUIRE(buttons.at(0) == fixture.button("menuFile"));
    REQUIRE(buttons.at(1) == fixture.button("menuGame"));
    REQUIRE(buttons.at(2) == fixture.button("menuView"));

    REQUIRE(buttons.at(0)->label() == QStringLiteral("File"));
    REQUIRE(buttons.at(1)->label() == QStringLiteral("Game"));
    REQUIRE(buttons.at(2)->label() == QStringLiteral("View"));

    REQUIRE(fixture.bar->button(MenuBar::Menu::Game) == buttons.at(1));
    REQUIRE(fixture.bar->menu(MenuBar::Menu::View) == fixture.view);

    // Every item 34 tall, centred in the 45 above the border: 5 above, where CSS has 5.5,
    // because Qt centres in whole pixels.
    for(MenuBarButton* item : buttons)
    {
        CAPTURE(item->objectName());
        REQUIRE(item->height() == menubar::itemHeight);
        REQUIRE(item->y() == 5);
    }

    // padding-left 16, then a gap of 4 between each, and nothing after View.
    REQUIRE(buttons.at(0)->x() == menubar::sidePadding);
    REQUIRE(buttons.at(1)->x() == buttons.at(0)->x() + buttons.at(0)->width() + menubar::itemGap);
    REQUIRE(buttons.at(2)->x() == buttons.at(1)->x() + buttons.at(1)->width() + menubar::itemGap);
    REQUIRE(fixture.bar->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size() == 3);
}

TEST_CASE("A bar button is as wide as its padding and label", "[menu-bar]")
{
    Fixture fixture;

    const QFontMetricsF metrics(menubar::labelFont());

    // .menubar .btn: padding 0 12 either side of the label, and no icon.
    for(MenuBarButton* item : fixture.bar->buttons())
    {
        CAPTURE(item->label());
        REQUIRE(item->width() == static_cast<int>(std::ceil(12.0 + metrics.horizontalAdvance(item->label()) + 12.0)));
    }

    // Belleza 17, page.html's Marcellus 17, through the headings' fallback chain.
    REQUIRE(menubar::labelFont().pixelSize() == 17);
    REQUIRE(menubar::labelFont().families().front() == QStringLiteral("Belleza"));
}

TEST_CASE("A label reads mnemonics the way Qt does", "[menu-bar]")
{
    // A '&' marks a mnemonic and is not shown, and "&&" is one literal '&'.
    REQUIRE(menubar::labelOf(QStringLiteral("New Game")) == QStringLiteral("New Game"));
    REQUIRE(menubar::labelOf(QStringLiteral("&View")) == QStringLiteral("View"));
    REQUIRE(menubar::labelOf(QStringLiteral("Joc && nou")) == QStringLiteral("Joc & nou"));
    REQUIRE(menubar::labelOf(QStringLiteral("&Vedere && jurnal")) == QStringLiteral("Vedere & jurnal"));
    REQUIRE(menubar::labelOf(QStringLiteral("&&&A")) == QStringLiteral("&A"));
    REQUIRE(menubar::labelOf(QStringLiteral("Trailing&")) == QStringLiteral("Trailing"));
    REQUIRE(menubar::labelOf(QString()).isEmpty());
}

TEST_CASE("A bar button claims no mnemonic of its own", "[menu-bar]")
{
    // A button registers a mnemonic in its text as a shortcut of its own, and beside the
    // window's Alt+F, Alt+G or Alt+V that is two claims, so neither would fire.
    REQUIRE(menubar::withoutMnemonic(QStringLiteral("&View")) == QStringLiteral("View"));
    REQUIRE(menubar::withoutMnemonic(QStringLiteral("&Vedere && jurnal")) == QStringLiteral("Vedere && jurnal"));
    REQUIRE(menubar::withoutMnemonic(QStringLiteral("Pause")) == QStringLiteral("Pause"));

    Fixture fixture;

    for(MenuBarButton* item : fixture.bar->buttons())
    {
        INFO(item->objectName().toStdString());
        REQUIRE(QKeySequence::mnemonic(item->text()).isEmpty());
    }
}

TEST_CASE("An ampersand in a bar label is drawn and sized as one", "[menu-bar]")
{
    Fixture fixture(QStringLiteral("&Fișier && joc"));

    const QFontMetricsF metrics(menubar::labelFont());

    MenuBarButton* file = fixture.button("menuFile");
    REQUIRE(file->label() == QStringLiteral("Fișier & joc"));
    REQUIRE(file->sizeHint().width() ==
            static_cast<int>(std::ceil(24.0 + metrics.horizontalAdvance(QStringLiteral("Fișier & joc")))));
}

TEST_CASE("A menu item's colours follow page.html's btn", "[menu-bar]")
{
    const Theme::TablePalette& palette = Theme::dark().table;

    // At rest: a gold icon and a cream label.
    const menubar::Colours rest = menubar::coloursFor(palette, true, false);
    REQUIRE(rest.icon == palette.gold);
    REQUIRE(rest.label == palette.cream);

    // Under the cursor or the keyboard: both gold-bright.
    const menubar::Colours lit = menubar::coloursFor(palette, true, true);
    REQUIRE(lit.icon == palette.goldBright);
    REQUIRE(lit.label == palette.goldBright);

    // Greyed, as the corner buttons are, whether or not the cursor is on it.
    for(const bool hovered : { false, true })
    {
        const menubar::Colours greyed = menubar::coloursFor(palette, false, hovered);
        REQUIRE(greyed.icon == palette.muted);
        REQUIRE(greyed.label == palette.muted);
    }
}

TEST_CASE("The menu bar paints its gradient and border", "[menu-bar]")
{
    Fixture fixture;

    const Theme::TablePalette& palette = Theme::dark().table;

    const QImage image = fixture.bar->grab().toImage();
    const qreal ratio = image.devicePixelRatio();

    // Right of View, where no item paints.
    MenuBarButton* view = fixture.button("menuView");
    const int x = static_cast<int>((view->x() + view->width() + 40) * ratio);

    const auto at = [&](int y){ return image.pixelColor(x, static_cast<int>(y * ratio)); };

    // linear-gradient(180deg, #2e1c10, #1a1009) over the 45 above the border.
    REQUIRE(near(at(0), palette.menuTop, 1));
    REQUIRE(near(at(44), palette.menuBottom, 1));

    // border-bottom: 1px #806137.
    REQUIRE(at(45) == palette.panelBorder);
}

TEST_CASE("The menu bar's shadow lies under it and lets the mouse through", "[menu-bar]")
{
    Fixture fixture;

    MenuBarShadow* shadow = fixture.bar->shadow();
    REQUIRE(shadow != nullptr);

    // A child of the window, not of the bar, so it can lie over the page: the whole width,
    // from the bar's bottom edge down as far as the blur reaches.
    REQUIRE(shadow->parentWidget() == &fixture.window);
    REQUIRE(shadow->geometry() == QRect(0, menubar::height, fixture.window.width(), menubar::shadowHeight()));
    REQUIRE(shadow->isVisible());
    REQUIRE(shadow->testAttribute(Qt::WA_TransparentForMouseEvents));

    // 0 6px 16px: three sigmas of 8 past an edge 6 down.
    REQUIRE(menubar::shadowHeight() == 30);

    // Half the colour 6 below the bar, where its moved box ends; about 0.77 at the bar's
    // own edge; and nothing left by the bottom.
    REQUIRE(menubar::shadowAt(6.0) == Catch::Approx(0.5));
    REQUIRE(menubar::shadowAt(0.0) == Catch::Approx(0.7739).margin(0.001));
    REQUIRE(menubar::shadowAt(30.0) < 0.002);

    // It follows the window's width.
    fixture.window.resize(700, 600);
    pump(50);
    REQUIRE(shadow->width() == 700);

    // And stays over the page beneath, whatever was laid there after it.
    QWidget* page = fixture.window.centralWidget();
    const QList<QObject*> siblings = fixture.window.children();
    REQUIRE(siblings.indexOf(shadow) > siblings.indexOf(page));
}

TEST_CASE("The menu bar's shadow paints the glint over the shadow", "[menu-bar]")
{
    Fixture fixture;

    const Theme::TablePalette& palette = Theme::dark().table;

    MenuBarShadow* shadow = fixture.bar->shadow();

    QImage image(shadow->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // Without the window's background, which render() lays down by default.
    shadow->render(&image, QPoint(), QRegion(), QWidget::RenderFlags());

    // The first row is the faint gold glint, over the shadow's darkest; the rest is
    // black, fading.
    const QColor glint = image.pixelColor(100, 0);
    REQUIRE(glint.red() > glint.blue());

    REQUIRE(image.pixelColor(100, 1).red() == 0);

    const qreal top = image.pixelColor(100, 1).alphaF();
    const qreal middle = image.pixelColor(100, 12).alphaF();
    const qreal bottom = image.pixelColor(100, 29).alphaF();

    REQUIRE(top == Catch::Approx(palette.menuShadow.alphaF() * menubar::shadowAt(1.5)).margin(0.02));
    REQUIRE(top > middle);
    REQUIRE(middle > bottom);
    REQUIRE(bottom < 0.01);
}

// page.html's .btn: `cursor: pointer`, and the plain arrow on a greyed item, which is
// Qt's: a disabled widget shows its parent's cursor.
TEST_CASE("A bar button points only while it can be pressed", "[menu-bar]")
{
    QWidget bar;
    bar.resize(240, 60);

    MenuBarButton& button = *new MenuBarButton(&bar);
    button.setText(QStringLiteral("Game"));
    button.resize(button.sizeHint());

    bar.show();
    QCoreApplication::processEvents();

    const CursorReturn resting = restOn(button);
    REQUIRE(shownOver(button) == Qt::PointingHandCursor);

    button.setEnabled(false);
    REQUIRE(shownOver(button) == Qt::ArrowCursor);

    button.setEnabled(true);
    REQUIRE(shownOver(button) == Qt::PointingHandCursor);
}

TEST_CASE("A press on a bar button opens its menu with the panel under the button", "[menu-bar]")
{
    Fixture fixture;

    int closed = 0;
    QObject::connect(fixture.bar, &MenuBar::menuClosed, [&closed]{ closed++; });

    MenuBarButton* game = fixture.button("menuGame");
    press(game, game->rect().center());
    pump(20);

    REQUIRE(fixture.game->isVisible());
    REQUIRE(fixture.bar->openMenu() == fixture.game);
    REQUIRE(QApplication::activePopupWidget() == fixture.game);
    REQUIRE(game->isLit());

    // Opened by the mouse, so nothing is chosen until the cursor or the keyboard moves.
    REQUIRE(fixture.game->activeAction() == nullptr);

    // The popup where the bar asked for it, and as wide as its rows.
    REQUIRE(fixture.game->pos() == fixture.bar->menuPosition(MenuBar::Menu::Game));
    REQUIRE(fixture.game->size() == fixture.game->sizeHint());

    // Its panel's left edge under the button's, a little below the bar; the popup itself
    // stops at the bar's edge, so it never covers a button.
    const QRect panel = fixture.game->panelRect().translated(fixture.game->pos());
    REQUIRE(panel.topLeft() == fixture.bar->mapToGlobal(QPoint(game->x(), menubar::height + dropdown::drop)));
    REQUIRE(fixture.game->y() == fixture.bar->mapToGlobal(QPoint(0, menubar::height)).y());

    // Esc closes it, and the bar says so once.
    key(fixture.game, Qt::Key_Escape);
    pump(20);

    REQUIRE_FALSE(fixture.game->isVisible());
    REQUIRE(fixture.bar->openMenu() == nullptr);
    REQUIRE_FALSE(game->isDown());
    REQUIRE(closed == 1);
}

TEST_CASE("Moving onto another bar button switches to its menu", "[menu-bar]")
{
    Fixture fixture;

    int closed = 0;
    QObject::connect(fixture.bar, &MenuBar::menuClosed, [&closed]{ closed++; });

    MenuBarButton* file = fixture.button("menuFile");
    MenuBarButton* game = fixture.button("menuGame");
    MenuBarButton* view = fixture.button("menuView");

    press(file, file->rect().center());
    pump(20);
    REQUIRE(fixture.file->isVisible());

    // Along the bar to View: the open menu has the mouse, and gives way.
    moveOver(fixture.file, middleOf(file) + QPoint(3, 0));
    REQUIRE(fixture.file->isVisible());

    moveOver(fixture.file, middleOf(view));
    pump(20);

    REQUIRE_FALSE(fixture.file->isVisible());
    REQUIRE(fixture.view->isVisible());
    REQUIRE(fixture.view->pos() == fixture.bar->menuPosition(MenuBar::Menu::View));
    REQUIRE(view->isDown());
    REQUIRE_FALSE(file->isDown());
    REQUIRE(view->isLit());
    REQUIRE_FALSE(file->isLit());

    // And back to Game, with no row chosen on the way.
    moveOver(fixture.view, middleOf(game));
    pump(20);

    REQUIRE(fixture.game->isVisible());
    REQUIRE(fixture.bar->openMenu() == fixture.game);
    REQUIRE(fixture.game->activeAction() == nullptr);

    // A switch is not a close.
    REQUIRE(closed == 0);
}

TEST_CASE("Left and Right switch menus round the bar", "[menu-bar]")
{
    Fixture fixture;

    // From the keyboard, the first row is chosen.
    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);
    REQUIRE(fixture.file->isVisible());
    REQUIRE(fixture.file->activeAction() == fixture.newGame);

    key(fixture.file, Qt::Key_Right);
    pump(20);
    REQUIRE(fixture.bar->openMenu() == fixture.game);
    REQUIRE(fixture.game->activeAction() == fixture.pause);

    key(fixture.game, Qt::Key_Right);
    pump(20);
    REQUIRE(fixture.bar->openMenu() == fixture.view);
    REQUIRE(fixture.view->activeAction() == fixture.log);

    // Past the end, round to the start; and back.
    key(fixture.view, Qt::Key_Right);
    pump(20);
    REQUIRE(fixture.bar->openMenu() == fixture.file);

    key(fixture.file, Qt::Key_Left);
    pump(20);
    REQUIRE(fixture.bar->openMenu() == fixture.view);
    REQUIRE_FALSE(fixture.file->isVisible());

    // Up and Down are still the menu's own.
    key(fixture.view, Qt::Key_Down);
    REQUIRE(fixture.view->activeAction() == fixture.cardArt);
}

TEST_CASE("A greyed row is passed over and never lit", "[menu-bar]")
{
    Fixture fixture;
    fixture.pause->setEnabled(false);

    fixture.bar->showMenu(MenuBar::Menu::Game);
    pump(20);
    REQUIRE(fixture.game->activeAction() == fixture.settings);

    // Under the cursor, still not chosen, and a click on it does nothing.
    int paused = 0;
    QObject::connect(fixture.pause, &QAction::triggered, [&paused]{ paused++; });

    const QPoint row = fixture.game->rowRect(fixture.pause).center();
    moveOver(fixture.game, fixture.game->mapToGlobal(row));
    REQUIRE(fixture.game->activeAction() != fixture.pause);

    press(fixture.game, row);
    release(fixture.game, row);
    pump(20);

    REQUIRE(paused == 0);
    REQUIRE(fixture.game->isVisible());
}

TEST_CASE("A click on a row triggers it and closes the menu", "[menu-bar]")
{
    Fixture fixture;

    int triggered = 0;
    QObject::connect(fixture.newGame, &QAction::triggered, [&triggered]{ triggered++; });

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);

    // At the row's right end, past where QMenu's own width would have ended.
    const QRect row = fixture.file->rowRect(fixture.newGame);
    const QPoint at(row.right() - 2, row.center().y());

    moveOver(fixture.file, fixture.file->mapToGlobal(at));
    REQUIRE(fixture.file->activeAction() == fixture.newGame);

    press(fixture.file, at);
    release(fixture.file, at);
    pump(20);

    REQUIRE(triggered == 1);
    REQUIRE_FALSE(fixture.file->isVisible());
}

TEST_CASE("A press on a menu's shadow closes it", "[menu-bar]")
{
    Fixture fixture;

    fixture.bar->showMenu(MenuBar::Menu::Game);
    pump(20);

    // Beside the panel, level with Settings: in the popup, outside the panel.
    const QPoint beside(fixture.game->panelRect().right() + 8, fixture.game->rowRect(fixture.settings).center().y());
    REQUIRE(fixture.game->rect().contains(beside));

    moveOver(fixture.game, fixture.game->mapToGlobal(beside));
    REQUIRE(fixture.game->activeAction() == nullptr);

    press(fixture.game, beside);
    pump(20);

    REQUIRE_FALSE(fixture.game->isVisible());
}

TEST_CASE("A menu lays its rows out to the panel's numbers", "[menu-bar]")
{
    Fixture fixture;

    fixture.bar->showMenu(MenuBar::Menu::View);
    pump(20);

    DropdownMenu* view = fixture.view;
    const QMargins margins = dropdown::shadowMargins();

    // Room for the shadow, 0 6px 16px: three sigmas of 8 to each side and below, past
    // its offset of 6 below; above, only the gap to the bar.
    REQUIRE(margins == QMargins(24, dropdown::drop, 24, 30));
    REQUIRE(view->panelRect() == view->rect().marginsRemoved(margins));

    // The rows 1 + 6 inside the panel, each 34 tall and the separator 9, edge to edge.
    const QRect panel = view->panelRect();
    const QRect log = view->rowRect(fixture.log);
    const QRect card = view->rowRect(fixture.cardArt);

    REQUIRE(log.topLeft() == panel.topLeft() + QPoint(7, 7));
    REQUIRE(log.height() == dropdown::rowHeight);
    REQUIRE(log.width() == view->rowWidth());
    REQUIRE(log.right() == panel.right() - 7);

    REQUIRE(card.top() == log.bottom() + 1 + dropdown::separatorHeight);
    REQUIRE(card.height() == dropdown::rowHeight);
    REQUIRE(panel.bottom() == card.bottom() + 7);

    // No row of View's is wider than the minimum.
    REQUIRE(view->rowWidth() == dropdown::minimumRowWidth);

    // A row with a shortcut is as wide as its contents: the padding, the icon column,
    // the label, the gap and the shortcut.
    const QFontMetricsF labels(menubar::labelFont());
    const QFontMetricsF shortcuts(dropdown::shortcutFont());
    const QString shortcut = fixture.settings->shortcut().toString(QKeySequence::NativeText);

    const qreal settings = 12.0 + 18.0 + 8.0 + labels.horizontalAdvance(QStringLiteral("Settings")) + 32.0
                           + shortcuts.horizontalAdvance(shortcut) + 12.0;
    REQUIRE(fixture.game->rowWidth() >= static_cast<int>(std::ceil(settings)));
    REQUIRE(fixture.game->sizeHint().width() == margins.left() + 14 + fixture.game->rowWidth() + margins.right());
}

TEST_CASE("A menu paints the theme's colours and not the system's", "[menu-bar]")
{
    Fixture fixture;

    // A system palette nothing of the menu's should show.
    QPalette garish;
    for(const QPalette::ColorRole role : { QPalette::Window, QPalette::Base, QPalette::Button, QPalette::Highlight,
                                           QPalette::Text, QPalette::WindowText, QPalette::ButtonText,
                                           QPalette::HighlightedText })
        garish.setColor(role, QColor(0xFF, 0x00, 0xFF));

    fixture.game->setPalette(garish);
    fixture.pause->setEnabled(false);

    fixture.bar->showMenu(MenuBar::Menu::Game);
    pump(20);

    // Rules under the keyboard.
    fixture.game->setActiveAction(fixture.rules);
    pump(20);

    const Theme::TablePalette& palette = Theme::dark().table;
    const QImage image = pictureOf(fixture.game);
    const QRect panel = fixture.game->panelRect();

    // Nothing magenta anywhere.
    REQUIRE(pixelsNear(image, image.rect(), QColor(0xFF, 0x00, 0xFF), 60) == 0);

    // The bar's gradient inside, between the border and the first row.
    const QColor top = image.pixelColor(panel.center().x(), panel.top() + 3);
    REQUIRE(top.alpha() == 255);
    REQUIRE(near(top, palette.menuTop, 6));

    // The border, a hairline, down the panel's straight sides.
    REQUIRE(near(image.pixelColor(panel.left(), panel.center().y()), palette.panelBorder, 2));
    REQUIRE(near(image.pixelColor(panel.right(), panel.center().y()), palette.panelBorder, 2));
    REQUIRE(near(image.pixelColor(panel.center().x(), panel.bottom()), palette.panelBorder, 2));

    // Rounded: the panel's very corner is not drawn on.
    REQUIRE(image.pixelColor(panel.topLeft()).alpha() < 128);

    // The shadow below the panel: dark, and fading.
    const QColor shadowNear = image.pixelColor(panel.center().x(), panel.bottom() + 4);
    const QColor shadowFar = image.pixelColor(panel.center().x(), panel.bottom() + 24);
    REQUIRE(shadowNear.alpha() > 40);
    REQUIRE(shadowNear.red() < 10);
    REQUIRE(shadowFar.alpha() < shadowNear.alpha());

    // The lit row: the bar's hover over the gradient, with its icon and label gold-bright.
    const QRect rules = fixture.game->rowRect(fixture.rules);
    const QColor behind = image.pixelColor(rules.right() - 3, rules.top() + 2);
    REQUIRE(near(behind, over(palette.menuHover, palette.menuBottom), 10));
    REQUIRE(pixelsNear(image, rules, palette.goldBright, 12) > 20);

    // A row at rest: a gold icon and a cream label.
    const QRect settings = fixture.game->rowRect(fixture.settings);
    REQUIRE(pixelsNear(image, settings.adjusted(12, 0, -(settings.width() - 30), 0), palette.gold, 12) > 10);
    REQUIRE(pixelsNear(image, settings.adjusted(38, 0, -80, 0), palette.cream, 12) > 20);
    REQUIRE(pixelsNear(image, settings, palette.goldBright, 6) == 0);

    // The greyed row muted throughout: no gold and no cream.
    const QRect pause = fixture.game->rowRect(fixture.pause);
    REQUIRE(pixelsNear(image, pause, palette.muted, 10) > 20);
    REQUIRE(pixelsNear(image, pause, palette.cream, 6) == 0);
    REQUIRE(pixelsNear(image, pause, palette.gold, 6) == 0);
}

TEST_CASE("A checked row shows a gold tick and an unchecked one none", "[menu-bar]")
{
    Fixture fixture;

    const Theme::TablePalette& palette = Theme::dark().table;

    fixture.bar->showMenu(MenuBar::Menu::View);
    pump(20);

    // The keyboard on Card art, so Log is drawn at rest.
    fixture.view->setActiveAction(fixture.cardArt);

    const QRect log = fixture.view->rowRect(fixture.log);
    const QRect column(log.left() + dropdown::rowPadding, log.top(), dropdown::iconSize, log.height());

    fixture.log->setChecked(false);
    pump(20);
    REQUIRE(pixelsNear(pictureOf(fixture.view), column, palette.gold, 40) == 0);

    fixture.log->setChecked(true);
    pump(20);
    REQUIRE(pixelsNear(pictureOf(fixture.view), column, palette.gold, 12) > 12);
}

TEST_CASE("Rows with and without an icon start their labels in one column", "[menu-bar]")
{
    Fixture fixture;

    const Theme::TablePalette& palette = Theme::dark().table;

    // Quit has an icon; a row added beside it has none.
    QAction* plain = new QAction(QStringLiteral("Mode"), &fixture.window);
    fixture.file->addItem(plain);

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);
    fixture.file->setActiveAction(nullptr);
    pump(20);

    const QImage image = pictureOf(fixture.file);
    const int column = dropdown::rowPadding + dropdown::iconSize + dropdown::iconGap;

    for(QAction* action : { fixture.newGame, fixture.quit, plain })
    {
        const QRect row = fixture.file->rowRect(action);
        const QRect label(row.left() + column - 4, row.top(), 40, row.height());

        CAPTURE(action->text().toStdString());
        const int first = firstColumnNear(image, label, palette.cream, 30);
        REQUIRE(first >= row.left() + column - 1);
        REQUIRE(first <= row.left() + column + 2);
    }
}

TEST_CASE("A menu follows the bar's theme", "[menu-bar]")
{
    Fixture fixture;

    Theme theme = Theme::dark();
    theme.table.menuTop = QColor(0x20, 0x40, 0x80);
    theme.table.menuBottom = QColor(0x20, 0x40, 0x80);
    fixture.bar->setTheme(theme);

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);

    const QImage image = pictureOf(fixture.file);
    const QRect panel = fixture.file->panelRect();
    REQUIRE(near(image.pixelColor(panel.center().x(), panel.top() + 3), theme.table.menuTop, 3));
}

namespace
{

// Where a menu's panel is on screen once Qt has placed the popup, and its button's left edge.
QRect placedPanel(DropdownMenu* menu)
{
    return menu->panelRect().translated(menu->pos());
}

int buttonLeft(MenuBarButton* button)
{
    return button->mapToGlobal(QPoint(0, 0)).x();
}

}

TEST_CASE("Every menu's panel lies under its button where Qt puts it", "[menu-bar]")
{
    Fixture fixture;

    for(const MenuBar::Menu which : { MenuBar::Menu::File, MenuBar::Menu::Game, MenuBar::Menu::View })
    {
        CAPTURE(static_cast<int>(which));

        fixture.bar->showMenu(which);
        pump(20);

        DropdownMenu* menu = fixture.bar->menu(which);
        REQUIRE(menu->pos() == fixture.bar->menuPosition(which));
        REQUIRE(placedPanel(menu).left() == buttonLeft(fixture.bar->button(which)));
        REQUIRE(placedPanel(menu).top() == fixture.bar->mapToGlobal(QPoint(0, menubar::height + dropdown::drop)).y());

        menu->hide();
        pump(20);
    }
}

TEST_CASE("A menu at a screen's edge gives up its shadow there and stays under its button", "[menu-bar]")
{
    Fixture fixture;

    const QRect area = fixture.window.screen()->availableGeometry();

    // The window's left edge on the screen's: File's button is 16 in, less than the shadow's
    // 24, and the popup would cross onto whatever lies left of the screen.
    // The offscreen platform keeps a 2 px frame left of the window.
    fixture.window.setGeometry(area.left(), area.top() + 100, 700, 500);
    pump(50);

    const int room = buttonLeft(fixture.bar->button(MenuBar::Menu::File)) - area.left();
    REQUIRE(room < dropdown::shadowMargins().left());

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);

    REQUIRE(fixture.file->x() == area.left());
    REQUIRE(fixture.file->shadowRoom().left() == room);
    REQUIRE(placedPanel(fixture.file).left() == buttonLeft(fixture.bar->button(MenuBar::Menu::File)));
    REQUIRE(fixture.file->shadowRoom().right() == dropdown::shadowMargins().right());

    fixture.file->hide();
    pump(20);

    // View's panel 10 short of the screen's right edge: 10 of the shadow's 24 there.
    MenuBarButton* view = fixture.bar->button(MenuBar::Menu::View);
    const int panelWidth = fixture.view->panelSize().width();
    const int left = area.right() + 1 - 10 - panelWidth - view->x();
    fixture.window.setGeometry(left, area.top() + 100, 700, 500);
    pump(50);

    fixture.bar->showMenu(MenuBar::Menu::View);
    pump(20);

    REQUIRE(fixture.view->shadowRoom().right() == 10);
    REQUIRE(fixture.view->geometry().right() == area.right());
    REQUIRE(placedPanel(fixture.view).left() == buttonLeft(view));

    fixture.view->hide();
    pump(20);

    // Away from the edges, the whole shadow again.
    fixture.window.setGeometry(area.left() + 40, area.top() + 100, 700, 500);
    pump(50);

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);
    REQUIRE(fixture.file->shadowRoom() == dropdown::shadowMargins());
    REQUIRE(placedPanel(fixture.file).left() == buttonLeft(fixture.bar->button(MenuBar::Menu::File)));
}

TEST_CASE("A tap on another bar button while a menu is open switches to its menu", "[menu-bar]")
{
    Fixture fixture;

    QPointingDevice* touchscreen = QTest::createTouchDevice();
    QWindow* window = fixture.window.windowHandle();

    // Through the window, as a finger arrives: Qt makes a mouse move and a press of it.
    const auto tap = [&](QWidget* widget)
    {
        const QPoint at = window->mapFromGlobal(middleOf(widget));
        QTest::touchEvent(window, touchscreen).press(0, at, window);
        QTest::touchEvent(window, touchscreen).release(0, at, window);
        pump(50);
    };

    tap(fixture.button("menuFile"));
    REQUIRE(fixture.bar->openMenu() == fixture.file);

    tap(fixture.button("menuGame"));
    REQUIRE(fixture.bar->openMenu() == fixture.game);
    REQUIRE_FALSE(fixture.file->isVisible());
}

TEST_CASE("A press on another bar button while a menu is open switches to its menu", "[menu-bar]")
{
    Fixture fixture;

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);

    // The press reaches the open menu, which has the mouse.
    MenuBarButton* view = fixture.button("menuView");
    press(fixture.file, fixture.file->mapFromGlobal(middleOf(view)));
    pump(20);

    REQUIRE(fixture.bar->openMenu() == fixture.view);
    REQUIRE_FALSE(fixture.file->isVisible());
}

TEST_CASE("A menu closed other than by a press asks that no later press be replayed", "[menu-bar]")
{
    // Qt decides as a popup closes whether to replay a press to what lies beneath; left to
    // decide on a keyboard close, a later click on the menu's button reopens the menu.
    Fixture fixture;

    fixture.bar->showMenu(MenuBar::Menu::File);
    pump(20);
    key(fixture.file, Qt::Key_Escape);
    pump(20);
    REQUIRE(fixture.file->testAttribute(Qt::WA_NoMouseReplay));

    // A switch along the bar closes the menu it leaves the same way.
    fixture.bar->showMenu(MenuBar::Menu::Game);
    pump(20);
    key(fixture.game, Qt::Key_Right);
    pump(20);
    REQUIRE(fixture.game->testAttribute(Qt::WA_NoMouseReplay));

    // A press on the page is replayed there, as Qt has it.
    fixture.view->setAttribute(Qt::WA_NoMouseReplay, false);
    const QPoint page = fixture.window.centralWidget()->mapToGlobal(QPoint(300, 300));
    press(fixture.view, fixture.view->mapFromGlobal(page));
    pump(20);
    REQUIRE_FALSE(fixture.view->isVisible());
    REQUIRE_FALSE(fixture.view->testAttribute(Qt::WA_NoMouseReplay));

    // And a press on the open menu's own button is not.
    fixture.bar->showMenu(MenuBar::Menu::Game);
    pump(20);
    MenuBarButton* game = fixture.button("menuGame");
    press(fixture.game, fixture.game->mapFromGlobal(middleOf(game)));
    pump(20);
    REQUIRE_FALSE(fixture.game->isVisible());
    REQUIRE(fixture.game->testAttribute(Qt::WA_NoMouseReplay));
}

TEST_CASE("Space on a bar button Tab reached opens its menu with the first row chosen", "[menu-bar]")
{
    Fixture fixture;

    int keyboardOnBar = -1;
    QObject::connect(fixture.bar, &MenuBar::menuClosed, [&keyboardOnBar](bool onBar){ keyboardOnBar = onBar; });

    MenuBarButton* game = fixture.button("menuGame");
    game->setFocus(Qt::TabFocusReason);
    pump(20);

    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(game, &space);
    pump(20);

    REQUIRE(fixture.bar->openMenu() == fixture.game);
    REQUIRE(fixture.game->activeAction() == fixture.pause);

    // Along the bar, the keyboard goes with it.
    key(fixture.game, Qt::Key_Right);
    pump(20);
    REQUIRE(fixture.bar->openMenu() == fixture.view);
    REQUIRE(fixture.window.focusWidget() == fixture.button("menuView"));

    key(fixture.view, Qt::Key_Escape);
    pump(50);
    REQUIRE(keyboardOnBar == 1);

    // A click on the same button is the mouse's: the keyboard is not the bar's.
    press(game, game->rect().center());
    pump(20);
    REQUIRE(fixture.game->activeAction() == nullptr);

    key(fixture.game, Qt::Key_Escape);
    pump(50);
    REQUIRE(keyboardOnBar == 0);
}
