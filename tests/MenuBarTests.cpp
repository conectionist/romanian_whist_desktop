#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "WindowDriving.h"
#include "paint/Theme.h"
#include "ui/MenuBar.h"

#include <QAction>
#include <QFontMetricsF>
#include <QImage>
#include <QMainWindow>
#include <QMenu>
#include <QPixmap>

#include <cmath>
#include <cstdlib>
#include <memory>

// Section 2.12's menu bar, the redesign's phase 9: page.html's .menubar, built as
// a widget of our own because a QMenuBar draws an item's icon or its text, never
// both. These hold it to the mock-up's numbers on its own, in a bare window; the
// window's use of it is MainWindowTests'.
namespace
{
using driving::pump;

// A window holding a bar with all five items, as MainWindow builds it.
struct Fixture
{
    QMainWindow window;
    QAction* newGame = new QAction(QStringLiteral("New Game"), &window);
    QAction* settings = new QAction(QStringLiteral("Settings"), &window);
    QAction* rules = new QAction(QStringLiteral("Rules"), &window);
    QMenu* view = new QMenu(QStringLiteral("&View"), &window);
    QAction* exitAction = new QAction(QStringLiteral("Exit"), &window);
    MenuBar* bar = nullptr;

    explicit Fixture(int width = 1200)
    {
        view->addAction(QStringLiteral("Log"));

        bar = new MenuBar({ newGame, settings, rules, view, exitAction }, &window);
        bar->setTheme(Theme::dark());
        window.setMenuWidget(bar);
        window.setCentralWidget(new QWidget(&window));

        window.resize(width, 600);
        window.show();
        pump(50);
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

}

TEST_CASE("The menu bar is 46 high with its items where page.html puts them", "[menu-bar]")
{
    Fixture fixture;

    REQUIRE(fixture.bar->height() == menubar::height);
    REQUIRE(fixture.bar->width() == fixture.window.width());

    MenuBarButton* newGame = fixture.button("menuNewGame");
    MenuBarButton* settings = fixture.button("menuSettings");
    MenuBarButton* rules = fixture.button("menuRules");
    MenuBarButton* view = fixture.button("menuView");
    MenuBarButton* exitButton = fixture.button("menuExit");

    REQUIRE(newGame != nullptr);
    REQUIRE(settings != nullptr);
    REQUIRE(rules != nullptr);
    REQUIRE(view != nullptr);
    REQUIRE(exitButton != nullptr);

    // Every item 34 tall, centred in the 45 above the border: 5 above, where CSS
    // has 5.5, because Qt centres in whole pixels.
    for(MenuBarButton* item : fixture.bar->buttons())
    {
        CAPTURE(item->objectName());
        REQUIRE(item->height() == menubar::itemHeight);
        REQUIRE(item->y() == 5);
    }

    // padding-left 16, then gap: 4 between the left-hand three.
    REQUIRE(newGame->x() == menubar::sidePadding);
    REQUIRE(settings->x() == newGame->x() + newGame->width() + menubar::itemGap);
    REQUIRE(rules->x() == settings->x() + settings->width() + menubar::itemGap);

    // padding-right 16 after Exit, and View in the Players switch's place before
    // it: that switch's margin-right of 14 and the gap of 4.
    REQUIRE(exitButton->x() + exitButton->width() == fixture.bar->width() - menubar::sidePadding);
    REQUIRE(exitButton->x() - (view->x() + view->width()) == menubar::viewMargin + menubar::itemGap);

    // And the stretch between the two groups.
    REQUIRE(view->x() > rules->x() + rules->width() + 100);
}

TEST_CASE("A menu item is as wide as its padding icon and label", "[menu-bar]")
{
    Fixture fixture;

    const QFontMetricsF metrics(menubar::labelFont());

    // .menubar .btn: padding 0 12, an 18 px icon and a gap of 8 before the label.
    MenuBarButton* newGame = fixture.button("menuNewGame");
    REQUIRE(newGame->text() == QStringLiteral("New Game"));
    REQUIRE(newGame->width() ==
            static_cast<int>(std::ceil(12.0 + 18.0 + 8.0 + metrics.horizontalAdvance(newGame->text()) +
                                       12.0)));

    // View has no icon, and shows no '&' from its menu's title.
    MenuBarButton* view = fixture.button("menuView");
    REQUIRE(view->label() == QStringLiteral("View"));
    REQUIRE(view->width() ==
            static_cast<int>(std::ceil(12.0 + metrics.horizontalAdvance(view->label()) + 12.0)));

    // The label's font is Marcellus 17, through section 2.3's chain.
    REQUIRE(menubar::labelFont().pixelSize() == 17);
    REQUIRE(menubar::labelFont().families().front() == QStringLiteral("Marcellus"));
}

TEST_CASE("A label reads mnemonics the way Qt does", "[menu-bar]")
{
    // PR #35's review, finding #15: a '&' marks a mnemonic and is not shown, and
    // "&&" is one literal '&'.
    REQUIRE(menubar::labelOf(QStringLiteral("New Game")) == QStringLiteral("New Game"));
    REQUIRE(menubar::labelOf(QStringLiteral("&View")) == QStringLiteral("View"));
    REQUIRE(menubar::labelOf(QStringLiteral("Joc && nou")) == QStringLiteral("Joc & nou"));
    REQUIRE(menubar::labelOf(QStringLiteral("&Vedere && jurnal")) == QStringLiteral("Vedere & jurnal"));
    REQUIRE(menubar::labelOf(QStringLiteral("&&&A")) == QStringLiteral("&A"));
    REQUIRE(menubar::labelOf(QStringLiteral("Trailing&")) == QStringLiteral("Trailing"));
    REQUIRE(menubar::labelOf(QString()).isEmpty());
}

TEST_CASE("An ampersand in a label is drawn and sized as one", "[menu-bar]")
{
    // PR #35's review, finding #15. Qt's text marks a mnemonic with '&' and
    // writes a literal one as "&&". The bar dropped every '&' from View's title,
    // so "&Vedere && jurnal" read "Vedere  jurnal", and drew an action's label
    // as it came, so "Joc && nou" read "Joc && nou". Shown with a throwaway
    // probe of the real bar before the fix.
    QMainWindow window;
    QAction* newGame = new QAction(QStringLiteral("Joc && nou"), &window);
    QAction* settings = new QAction(QStringLiteral("Settings"), &window);
    QAction* rules = new QAction(QStringLiteral("Rules"), &window);
    QMenu* view = new QMenu(QStringLiteral("&Vedere && jurnal"), &window);
    QAction* exitAction = new QAction(QStringLiteral("Exit"), &window);
    view->addAction(QStringLiteral("Log"));

    MenuBar* bar = new MenuBar({ newGame, settings, rules, view, exitAction }, &window);
    bar->setTheme(Theme::dark());
    window.setMenuWidget(bar);
    window.setCentralWidget(new QWidget(&window));
    window.resize(1200, 600);
    window.show();
    pump(50);

    const QFontMetricsF metrics(menubar::labelFont());
    const auto widthOf = [&metrics](const QString& label, bool icon)
    {
        return static_cast<int>(std::ceil(12.0 + (icon ? 18.0 + 8.0 : 0.0) +
                                          metrics.horizontalAdvance(label) + 12.0));
    };

    MenuBarButton* first = bar->findChild<MenuBarButton*>(QStringLiteral("menuNewGame"));
    MenuBarButton* viewButton = bar->findChild<MenuBarButton*>(QStringLiteral("menuView"));
    REQUIRE(first != nullptr);
    REQUIRE(viewButton != nullptr);

    REQUIRE(first->sizeHint().width() == widthOf(QStringLiteral("Joc & nou"), true));
    REQUIRE(viewButton->sizeHint().width() == widthOf(QStringLiteral("Vedere & jurnal"), false));

    // And an action renamed later, through setText().
    settings->setText(QStringLiteral("Set && go"));
    pump(10);

    MenuBarButton* second = bar->findChild<MenuBarButton*>(QStringLiteral("menuSettings"));
    REQUIRE(second->sizeHint().width() == widthOf(QStringLiteral("Set & go"), true));
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

TEST_CASE("A menu item follows its action", "[menu-bar]")
{
    Fixture fixture;

    MenuBarButton* exitButton = fixture.button("menuExit");

    // Greyed with its action, so Exit says when it can't pause.
    fixture.exitAction->setEnabled(false);
    REQUIRE_FALSE(exitButton->isEnabled());

    fixture.exitAction->setEnabled(true);
    REQUIRE(exitButton->isEnabled());

    // A click is the action's trigger.
    int triggered = 0;
    QObject::connect(fixture.exitAction, &QAction::triggered, [&triggered]{ triggered++; });

    exitButton->click();
    REQUIRE(triggered == 1);

    // And never takes the keyboard from the bid strip on the way.
    REQUIRE(exitButton->focusPolicy() == Qt::TabFocus);
}

TEST_CASE("The menu bar paints its gradient and border", "[menu-bar]")
{
    Fixture fixture;

    const Theme::TablePalette& palette = Theme::dark().table;

    const QImage image = fixture.bar->grab().toImage();
    const qreal ratio = image.devicePixelRatio();

    // Between the two groups, where no item paints.
    MenuBarButton* rules = fixture.button("menuRules");
    MenuBarButton* view = fixture.button("menuView");
    const int x = static_cast<int>((rules->x() + rules->width() + view->x()) / 2 * ratio);

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

    // A child of the window, not of the bar, so it can lie over the page: the
    // whole width, from the bar's bottom edge down as far as the blur reaches.
    REQUIRE(shadow->parentWidget() == &fixture.window);
    REQUIRE(shadow->geometry() ==
            QRect(0, menubar::height, fixture.window.width(), menubar::shadowHeight()));
    REQUIRE(shadow->isVisible());
    REQUIRE(shadow->testAttribute(Qt::WA_TransparentForMouseEvents));

    // 0 6px 16px: three sigmas of 8 past an edge 6 down.
    REQUIRE(menubar::shadowHeight() == 30);

    // Half the colour 6 below the bar, where its moved box ends; about 0.77 at the
    // bar's own edge; and nothing left by the bottom.
    REQUIRE(menubar::shadowAt(6.0) == Catch::Approx(0.5));
    REQUIRE(menubar::shadowAt(0.0) == Catch::Approx(0.7739).margin(0.001));
    REQUIRE(menubar::shadowAt(30.0) < 0.002);

    // It follows the window's width.
    fixture.window.resize(900, 600);
    pump(50);
    REQUIRE(shadow->width() == 900);

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

    // The first row is the faint gold glint, over the shadow's darkest; the rest
    // is black, fading.
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
