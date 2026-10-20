#include <catch2/catch_test_macros.hpp>

#include "TableFixtures.h"

#include "paint/Fonts.h"
#include "paint/Icons.h"
#include "paint/Theme.h"
#include "table/ScoreboardItem.h"
#include "table/SeatLayout.h"
#include "table/TableButtonItem.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"
#include "table/TableView.h"
#include "table/TrickPanelItem.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QSize>

#include <cmath>
#include <memory>
#include <vector>

// THE CORNER PANELS (the table redesign's sections 2.9 to 2.11): pinned to the
// window's corners rather than to the table, and drawn at the table's scale.
//
// What has a right answer is where they sit - so many pixels in from the corner,
// whatever the window's shape - and what a button does with the cursor and a
// click. How they look is checked by eye against the mock-up's screenshots.
namespace
{

// THROUGH THE SCENE, as TableSceneTests' presses go: which item takes the event is
// part of the question.
void pressAndRelease(QGraphicsScene& scene, QPointF press, QPointF release)
{
    QGraphicsSceneMouseEvent down(QEvent::GraphicsSceneMousePress);

    down.setScenePos(press);
    down.setButton(Qt::LeftButton);
    down.setButtons(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &down);

    QGraphicsSceneMouseEvent up(QEvent::GraphicsSceneMouseRelease);

    up.setScenePos(release);
    up.setButton(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &up);
}

void hoverAt(QGraphicsScene& scene, QPointF where)
{
    QGraphicsSceneHoverEvent hover(QEvent::GraphicsSceneHoverMove);

    hover.setScenePos(where);

    QCoreApplication::sendEvent(&scene, &hover);
}

// A button in a scene of its own, at the origin.
struct LoneButton
{
    QGraphicsScene scene;
    TableButtonItem* button = new TableButtonItem(icons::Icon::RoundHistory,
                                                  QStringLiteral("Round history"), nullptr);
    int clicks = 0;

    LoneButton()
    {
        scene.addItem(button);

        // A lambda rather than QSignalSpy, for BidStripTests' reason: QSignalSpy is
        // Qt6::Test, a whole module for one count.
        QObject::connect(button, &TableButtonItem::clicked, [this] { clicks++; });
    }

    QPointF middle() const
    {
        return QRectF(QPointF(), button->size()).center();
    }
};

// A corner panel's own rect in table space: the panel as drawn, without the
// shadow round it. Every corner panel has its top-left at its origin.
template<typename Panel>
QRectF inScene(const Panel& panel)
{
    return panel.mapRectToScene(QRectF(QPointF(), panel.size()));
}

// And where that lands in the view's viewport, in pixels.
template<typename Panel>
QRectF onScreen(const TableView& view, const Panel& panel)
{
    return view.viewportTransform().mapRect(inScene(panel));
}

// A table of `seats`, you at the bottom, mid-play, with a trump: the scoreboard
// at its tallest for that many rows.
GameSnapshot tableOf(unsigned int seats)
{
    using romanian_whist::Card;
    using romanian_whist::Rank;
    using romanian_whist::Suit;

    GameSnapshot snapshot = fullTable();

    snapshot.seats.resize(seats);
    snapshot.betsPlaced = seats;
    snapshot.hasTrump = true;
    snapshot.trump = Card(Rank::Queen, Suit::Clubs);

    for(unsigned int i = 0 ; i < seats ; i++)
        snapshot.seats[i].bidOrder = i + 1;

    snapshot.table.clear();

    return snapshot;
}

// Windows of every shape: the screenshot size, a laptop, the size the suite plays
// at, an ultra-wide, and one taller than it is wide.
const QSize shapes[] = { QSize(1920, 1080), QSize(1440, 900), QSize(1000, 700),
                         QSize(2560, 1080), QSize(800, 1200) };

}

TEST_CASE("A button is as wide as its icon and its label", "[hud]")
{
    // .panel.btn: a border of 1 and padding of 22 either side, the 24-unit icon and
    // 12 after it, then the label in Marcellus 21. 76 tall, border included.
    QFont label = fonts::heading();
    label.setPixelSize(tablebutton::labelSize);

    for(const char* text : { "Round history", "Last trick" })
    {
        INFO(text);

        const QSizeF size = tablebutton::sizeFor(QString::fromUtf8(text));
        const qreal advance = QFontMetricsF(label).horizontalAdvance(QString::fromUtf8(text));

        REQUIRE(size.height() == tablebutton::height);
        REQUIRE(std::abs(size.width() - (82.0 + advance)) < 1e-9);
    }
}

TEST_CASE("A disabled button takes no click and does not light up", "[hud]")
{
    // Until phase 11 gives them their sheets: a control that looked live and did
    // nothing would be a defect of its own.
    LoneButton lone;

    lone.button->setEnabled(false);

    hoverAt(lone.scene, lone.middle());
    pressAndRelease(lone.scene, lone.middle(), lone.middle());

    REQUIRE_FALSE(lone.button->isHovered());
    REQUIRE(lone.clicks == 0);
}

TEST_CASE("An enabled button lights up under the cursor and is clicked", "[hud]")
{
    LoneButton lone;

    hoverAt(lone.scene, lone.middle());
    REQUIRE(lone.button->isHovered());

    pressAndRelease(lone.scene, lone.middle(), lone.middle());
    REQUIRE(lone.clicks == 1);

    // Off it, it goes out again.
    hoverAt(lone.scene, QPointF(-200.0, -200.0));
    REQUIRE_FALSE(lone.button->isHovered());
}

TEST_CASE("A press let go off the button is not a click", "[hud]")
{
    // As any button: the player who presses and thinks better of it drags away.
    LoneButton lone;

    pressAndRelease(lone.scene, lone.middle(), QPointF(-200.0, -200.0));

    REQUIRE(lone.clicks == 0);
}

TEST_CASE("Disabling a lit button puts it out", "[hud]")
{
    LoneButton lone;

    hoverAt(lone.scene, lone.middle());
    REQUIRE(lone.button->isHovered());

    lone.button->setEnabled(false);

    REQUIRE_FALSE(lone.button->isHovered());
}

TEST_CASE("A lit button is a different picture and the same size", "[hud]")
{
    // The hover redraws it - and the bounding rect does not move, so the scene is
    // not re-indexed every time the cursor crosses it.
    LoneButton lone;

    const QRectF bounds = lone.button->boundingRect();
    const QImage before = [&]
    {
        QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        lone.scene.render(&painter, QRectF(), QRectF(-50.0, -50.0, 400.0, 200.0));
        return image;
    }();

    hoverAt(lone.scene, lone.middle());

    QImage after(400, 200, QImage::Format_ARGB32_Premultiplied);
    after.fill(Qt::transparent);
    {
        QPainter painter(&after);
        lone.scene.render(&painter, QRectF(), QRectF(-50.0, -50.0, 400.0, 200.0));
    }

    REQUIRE(lone.button->boundingRect() == bounds);
    REQUIRE(after != before);
}

TEST_CASE("The corner panels wait for a view and a game", "[hud]")
{
    const auto snapshot = std::make_shared<const GameSnapshot>(fullTable());

    // A scene drawn with no view has no corners to pin to.
    TableScene unseen;
    unseen.setSnapshot(snapshot);

    REQUIRE_FALSE(unseen.historyButton()->isVisible());
    REQUIRE_FALSE(unseen.lastTrickButton()->isVisible());

    // A view with no game yet: the table, and nothing in its corners.
    TableScene scene;
    TableView view(&scene);

    view.resize(1000, 700);
    view.show();
    QCoreApplication::processEvents();

    REQUIRE_FALSE(scene.historyButton()->isVisible());

    scene.setSnapshot(snapshot);

    REQUIRE(scene.historyButton()->isVisible());
    REQUIRE(scene.lastTrickButton()->isVisible());
}

TEST_CASE("The corner buttons are disabled until they have sheets to open", "[hud]")
{
    // Phase 11 enables them.
    TableScene scene;

    REQUIRE_FALSE(scene.historyButton()->isEnabled());
    REQUIRE_FALSE(scene.lastTrickButton()->isEnabled());
}

// SECTION 2.1: 8 pixels in from the window's left and bottom edges, whatever its
// shape - the mock-up's fit() recomputes them on every resize, and the buttons'
// place in table space changes with the window while the table's does not.
TEST_CASE("The corner buttons stay eight pixels in from the bottom-left corner", "[hud]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));

    TableView view(&scene);
    view.show();

    for(const QSize& size : shapes)
    {
        INFO("window " << size.width() << "x" << size.height());

        view.resize(size);
        QCoreApplication::processEvents();

        const qreal scale = view.transform().m11();
        const QRectF history = onScreen(view, *scene.historyButton());
        const QRectF last = onScreen(view, *scene.lastTrickButton());

        REQUIRE(std::abs(history.left() - tablespace::hudInset) < 1e-6);
        REQUIRE(std::abs(history.bottom() - (view.viewport()->height() - tablespace::hudInset)) < 1e-6);

        // Side by side, 14 table units apart, bottoms level.
        REQUIRE(std::abs(last.left() - (history.right() + tablespace::buttonGap * scale)) < 1e-6);
        REQUIRE(std::abs(last.bottom() - history.bottom()) < 1e-6);

        // At the table's own scale: 76 units tall.
        REQUIRE(std::abs(history.height() - tablebutton::height * scale) < 1e-6);
    }
}

TEST_CASE("The scoreboard stays eight pixels in from the top-left corner", "[hud]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));

    TableView view(&scene);
    view.show();

    for(const QSize& size : shapes)
    {
        INFO("window " << size.width() << "x" << size.height());

        view.resize(size);
        QCoreApplication::processEvents();

        const qreal scale = view.transform().m11();
        const ScoreboardItem& scoreboard = *scene.scoreboardItem();
        const QRectF placed = onScreen(view, scoreboard);

        REQUIRE(scoreboard.isVisible());
        REQUIRE(std::abs(placed.left() - tablespace::hudInset) < 1e-6);
        REQUIRE(std::abs(placed.top() - tablespace::hudInset) < 1e-6);

        // Drawn 1.2 times the table, as page.html's .score is.
        REQUIRE(std::abs(placed.width() - scoreboard.size().width() * tablespace::hudScale * scale) < 1e-6);
    }
}

// THE PLAN'S PROMISE FOR PHASE 8: at the window sizes people play at, nothing in a
// corner covers a seat, and nothing in a corner is cut off by the window. Each
// size as a bare view the size of the whole window, which is a little stricter
// than the real one: the menu bar takes height, and a wider viewport leaves the
// corners more room beside the table.
TEST_CASE("The corner panels sit inside the window and clear of every seat", "[hud]")
{
    const QSize windows[] = { QSize(1920, 1080), QSize(1920, 1200), QSize(1440, 900) };

    for(unsigned int seats = 3 ; seats <= 6 ; seats++)
    {
        TableScene scene;
        scene.setTheme(Theme::dark());
        scene.setSnapshot(std::make_shared<const GameSnapshot>(tableOf(seats)));

        TableView view(&scene);
        view.show();

        for(const QSize& window : windows)
        {
            INFO(seats << " seats, window " << window.width() << "x" << window.height());

            view.resize(window);
            QCoreApplication::processEvents();

            const QRectF viewport(QPointF(), QSizeF(view.viewport()->size()));

            std::vector<QRectF> corners = { inScene(*scene.scoreboardItem()),
                                            inScene(*scene.trickPanelItem()),
                                            inScene(*scene.historyButton()),
                                            inScene(*scene.lastTrickButton()) };

            for(const QRectF& corner : corners)
            {
                REQUIRE(viewport.contains(view.viewportTransform().mapRect(corner)));

                for(unsigned int seat = 0 ; seat < seats ; seat++)
                {
                    INFO("seat " << seat);

                    REQUIRE_FALSE(corner.intersects(seatlayout::panelRect(seats, 0, seat)));
                }
            }
        }
    }
}

TEST_CASE("The trick panel stays eight pixels in from the top-right corner", "[hud]")
{
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));

    TableView view(&scene);
    view.show();

    for(const QSize& size : shapes)
    {
        INFO("window " << size.width() << "x" << size.height());

        view.resize(size);
        QCoreApplication::processEvents();

        const qreal scale = view.transform().m11();
        const TrickPanelItem& panel = *scene.trickPanelItem();
        const QRectF placed = onScreen(view, panel);

        REQUIRE(panel.isVisible());
        REQUIRE(std::abs(placed.right() - (view.viewport()->width() - tablespace::hudInset)) < 1e-6);
        REQUIRE(std::abs(placed.top() - tablespace::hudInset) < 1e-6);

        // 264 units, drawn 1.2 times the table, as page.html's .trickinfo is.
        REQUIRE(std::abs(placed.width() - trickpanel::width * tablespace::hudScale * scale) < 1e-6);
    }
}
