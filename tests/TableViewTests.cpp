#include <catch2/catch_test_macros.hpp>

#include "TableFixtures.h"

#include "table/TableView.h"
#include "table/BidStripItem.h"
#include "table/RackItem.h"
#include "table/SeatItem.h"
#include "table/LeadMarkItem.h"
#include "table/ScoreboardItem.h"
#include "table/TableButtonItem.h"
#include "table/TrickPanelItem.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"
#include "engine/Prompts.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QGraphicsItem>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QTimer>
#include <QtMath>

#include <cmath>
#include <cstddef>
#include <map>
#include <memory>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// THE SCALE ITSELF, since the table redesign's phase 2: the view fits a fixed
// table to the window, centred, and a resize changes that scale and nothing in the
// scene.
//
// IT ALSO PLACED THE BID STRIP until phase 10, which put the strip on the felt as
// a scene item (BidStripTests): the two cases on where the view floated it, and
// how tall it was when squeezed, went with it.

namespace
{

GameSnapshot seatedTable(unsigned int seats, int humanSeat)
{
    GameSnapshot snapshot;

    snapshot.humanSeat = humanSeat;
    snapshot.roundNumber = 1;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.seats.resize(seats);

    for(unsigned int i = 0 ; i < seats ; i++)
        snapshot.seats[i].seatIndex = i;

    return snapshot;
}

// Windows of every shape the scale has to handle: the screenshot size, a laptop,
// the size the suite plays at, an ultra-wide, and one taller than it is wide. And,
// since phase 12, the two other sizes the plan names: 16:10 at 1920 and 2560 x 1440.
// `shapes[0]` stays the screenshot size, which a case below resizes to.
const QSize shapes[] = { QSize(1920, 1080), QSize(1440, 900), QSize(1000, 700),
                         QSize(2560, 1080), QSize(800, 1200), QSize(1920, 1200),
                         QSize(2560, 1440) };

}

// SECTION 2.1, and the mock-up's fit(): one scale for both axes, the largest that
// fits the fit box inside the viewport's margins, and the table in the middle.
// Asked of the view through its own mapping, so it is what is drawn and not what
// the scale function returns.
TEST_CASE("The table is scaled to fit the window and centred in it", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    TableView view(&scene);
    view.show();

    for(const QSize& size : shapes)
    {
        INFO("window " << size.width() << "x" << size.height());

        view.resize(size);

        const QSize viewport = view.viewport()->size();

        // The fit box's corners, in viewport pixels. Whole pixels, so every bound
        // below allows the one a corner can round by.
        const QRect box = view.mapFromScene(scene.sceneRect()).boundingRect();

        const qreal across = box.right() - box.left();
        const qreal down = box.bottom() - box.top();

        // Inside the viewport, margins and all.
        REQUIRE(box.left() >= tablespace::marginX - 1.0);
        REQUIRE(box.right() <= viewport.width() - tablespace::marginX + 1.0);
        REQUIRE(box.top() >= tablespace::marginTop - 2.0);
        REQUIRE(box.bottom() <= viewport.height() - tablespace::marginBottom + 2.0);

        // ROUND: one scale for both axes, so the box keeps its own shape.
        REQUIRE(std::abs(across / down - tablespace::fitBox.width() / tablespace::fitBox.height())
                < 0.01);

        // AS LARGE AS FITS: touching the margins on the axis that limits it.
        const bool widthLimited =
            std::abs(across - (viewport.width() - 2.0 * tablespace::marginX)) <= 2.0;
        const bool heightLimited =
            std::abs(down - (viewport.height() - tablespace::marginTop - tablespace::marginBottom))
            <= 2.0;

        REQUIRE((widthLimited || heightLimited));

        // AND CENTRED IN THE PADDED AREA, as fit() centres it: the margins are 16
        // either side but 10 above and 8 below, so the table's centre sits a
        // pixel below the viewport's. EXACTLY, through the transform itself
        // rather than a rounded QPoint: QGraphicsView's own centring halved the
        // viewport in whole pixels and truncated its scroll, and was up to a
        // pixel off on either axis.
        const QPointF centre = view.viewportTransform().map(tablespace::centre);

        const qreal paddedMiddle =
            tablespace::marginTop
            + (viewport.height() - tablespace::marginTop - tablespace::marginBottom) / 2.0;

        REQUIRE(std::abs(centre.x() - viewport.width() / 2.0) <= 1e-6);
        REQUIRE(std::abs(centre.y() - paddedMiddle) <= 1e-6);
    }
}

// THE PHASE'S WHOLE PROMISE: a resize scales the picture and moves nothing in it.
// Before the redesign every resize recomputed the card size and every position
// from it, so a window dragged a few pixels could reflow the table; now the
// layout is table space and only the view's transform changes. Every item the
// scene holds, asked for where it is, before and after.
TEST_CASE("A resize moves nothing in the scene", "[table]")
{
    GameSnapshot snapshot = seatedTable(6, 1);

    snapshot.hand = { HandEntry{}, HandEntry{} };
    snapshot.table = { PlayedCardView{ 2 }, PlayedCardView{ 3 } };

    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    TableView view(&scene);
    view.show();
    view.resize(shapes[0]);

    std::map<const QGraphicsItem*, QRectF> before;

    for(const QGraphicsItem* item : scene.items())
        before[item] = item->sceneBoundingRect();

    REQUIRE(before.size() > 6);

    for(const QSize& size : shapes)
    {
        INFO("window " << size.width() << "x" << size.height());

        view.resize(size);

        REQUIRE(scene.items().size() == static_cast<qsizetype>(before.size()));

        // ALL BUT THE CORNER PANELS, which belong to the window's corners rather
        // than to the table (section 2.1), and are meant to move: HudTests holds
        // them to their corners.
        for(const QGraphicsItem* item : scene.items())
        {
            if(item->zValue() != tablespace::z::hud)
                REQUIRE(item->sceneBoundingRect() == before.at(item));
        }
    }
}

// ---- the room, which the view draws ----
//
// THE ROOM IS THE VIEWPORT'S BACKGROUND, not the table's: the fit box keeps the
// table's shape, so a window of any other shape shows bands beside or above it,
// and the room fills those too. So it is drawn by the view, which knows its own
// viewport, rather than by the scene, which had to work out the viewport's size
// from whatever device it was handed - and guessed the whole device when that
// was not a viewport.
//
// AND AT A RATIO OF 2, because that is where the blit can go wrong. A drawPixmap
// TARGET rect is logical and a SOURCE rect is not - Qt reads the source in the
// pixmap's own pixels - so passing the same rect as both samples the top-left
// quarter of the room and stretches it over the whole viewport. That is invisible
// at a ratio of 1, which is where the suite and the developer's screen both are.

namespace
{

const QSize roomView(320, 200);

constexpr qreal roomRatio = 2.0;

// A ratio-2 canvas the size of the viewport: 640 x 400 physical pixels laid out
// over 320 x 200 logical ones, which is what a viewport on a retina screen is.
QImage roomCanvas()
{
    QImage image(roomView * roomRatio, QImage::Format_ARGB32);

    image.setDevicePixelRatio(roomRatio);
    image.fill(Qt::transparent);

    return image;
}

QImage expectedRoom(QSize size, qreal ratio)
{
    return Theme::light().room(size, ratio).toImage().convertToFormat(QImage::Format_ARGB32);
}

// A view of an empty table WITH THE TABLE ITSELF HIDDEN, so the room is the only
// thing drawn. These cases are about the room and how it is blitted; since the
// redesign's phase 3 the table lies over it, and would be all they compared.
struct RoomView
{
    TableScene scene;
    TableView view{ &scene };

    RoomView()
    {
        scene.setTheme(Theme::light());

        for(QGraphicsItem* item : scene.items())
        {
            if(dynamic_cast<TableTopItem*>(item))
                item->hide();
        }

        view.resize(roomView);
        view.show();
    }
};

// Part of the viewport, rendered onto the same part of the canvas: source and
// target are one logical rect, so the canvas's ratio is the only scale between
// them - which is what painting the viewport on screen is.
void renderRegion(TableView& view, QImage& canvas, const QRect& region)
{
    QPainter painter(&canvas);

    view.render(&painter, QRectF(region), region);
}

}

TEST_CASE("The room is blitted 1:1 at a device pixel ratio of 2", "[table]")
{
    RoomView table;

    REQUIRE(table.view.viewport()->size() == roomView);

    QImage rendered = roomCanvas();
    renderRegion(table.view, rendered, QRect(QPoint(0, 0), roomView));

    // Painted at 640 x 400, drawn over 320 x 200 logical pixels: one pixmap pixel
    // per canvas pixel, and no trace of the table's transform, because the room
    // belongs to the viewport.
    REQUIRE(rendered == expectedRoom(roomView, roomRatio));
}

// The size is only half of it. The source rect is also OFFSET by the exposed
// rect, so a partial repaint - which is what every repaint is once a card is
// moving over the table - reads from the wrong place besides being the wrong
// size. A fix that scaled the size and left the origin alone would pass the case
// above and fail every frame of a real game.
TEST_CASE("A partial repaint samples its own region of the room", "[table]")
{
    RoomView table;

    const QRect exposed(160, 0, 160, 200);

    QImage rendered = roomCanvas();
    renderRegion(table.view, rendered, exposed);

    // The same region, in the physical pixels both images are addressed in.
    const QRect physical(exposed.topLeft() * roomRatio, exposed.size() * roomRatio);

    REQUIRE(rendered.copy(physical) == expectedRoom(roomView, roomRatio).copy(physical));

    // And nothing outside it was touched, which is what the exposed rect is for.
    REQUIRE(rendered.pixelColor(0, 0).alpha() == 0);
}

// THE LETTERBOX. The corner of the viewport is outside the table as it is drawn,
// and a room drawn into the scene rect would leave it unpainted - on a wide
// screen, two black strips down the sides of the table.
TEST_CASE("The room covers the viewport, not just the table", "[table]")
{
    RoomView table;

    QImage rendered = roomCanvas();
    renderRegion(table.view, rendered, QRect(QPoint(0, 0), roomView));

    // The premise: the corner is outside the table.
    REQUIRE_FALSE(table.view.mapFromScene(table.scene.sceneRect()).boundingRect()
                      .contains(QPoint(0, 0)));

    REQUIRE(rendered.pixelColor(0, 0).alpha() == 255);
    REQUIRE(rendered.pixelColor(0, 0) == expectedRoom(roomView, roomRatio).pixelColor(0, 0));
}

// THE VIEW'S ROOM WHEREVER IT IS DRAWN. Rendered into one corner of a larger
// image, the room is the viewport's - its gradient centred on the view - and not
// a corner cut from a room the size of the whole image, which is what the scene
// drew when it took the device it was painting on for the viewport.
TEST_CASE("Rendered into part of a larger image, the room is still the view's", "[table]")
{
    RoomView table;

    QImage rendered(roomView * 2, QImage::Format_ARGB32);
    rendered.fill(Qt::transparent);

    const QRect corner(QPoint(roomView.width(), roomView.height()), roomView);

    {
        QPainter painter(&rendered);
        table.view.render(&painter, QRectF(corner), QRect(QPoint(0, 0), roomView));
    }

    REQUIRE(rendered.copy(corner) == expectedRoom(roomView, 1.0));
}

// ---- settling a resize, added in the redesign's phase 3 ----
//
// EVERY STEP OF A WINDOW DRAG CHANGES THE SCALE, and every pixmap on the table is
// rasterized at the screen's ratio times the scale - so a drag drawn honestly
// paints every card from its SVG and composes the table again on every frame. The
// view freezes a scale the drag has painted at instead: the pixmaps already made
// are drawn scaled, and everything is rasterized once at the new scale when the
// window stops. A resize's FIRST step is drawn sharp, so a one-off resize -
// maximizing, a dock shown - is never soft. Counted here rather than timed,
// through the read-only counts the renderer and the table keep: a timing would be
// a fact about the machine.

namespace
{

// fullTable() (TableFixtures.h), in the dark theme.
void dealFullTable(TableScene& scene)
{
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(fullTable()));
}

// Shown at `size` and painted once, so every pixmap it needs is in its cache.
void showAndPaint(TableView& view, QSize size)
{
    view.resize(size);
    view.show();
    QCoreApplication::processEvents();

    view.viewport()->grab();
}

// The ratio a pixmap drawn in `view` now, at its live scale, is made at.
qreal liveRatio(const TableView& view)
{
    return view.viewport()->devicePixelRatioF() * view.transform().m11();
}

// Whether the table's picture and a card from your hand - fullTable()'s Ace of
// Spades - are both cached at `ratio`: asked for there, neither count moves.
bool cachedAt(const TableScene& scene, qreal ratio)
{
    const std::size_t tables = scene.tableTop()->renders();
    scene.tableTop()->picture(ratio);

    const CardRenderer& cards = scene.cardRenderer();
    const std::size_t rasterized = cards.rasterizations();
    cards.face(Card(Rank::Ace, Suit::Spades), cards.sizeForHeight(tablespace::handCardHeight),
               ratio);

    return scene.tableTop()->renders() == tables && cards.rasterizations() == rasterized;
}

// A view of fullTable(), shown, fitted, and painted once at its settled scale -
// so every pixmap it needs is in its cache before a case begins.
struct ShownTable
{
    TableScene scene;
    TableView view{ &scene };

    explicit ShownTable(QSize size = QSize(1000, 700))
    {
        dealFullTable(scene);
        showAndPaint(view, size);
    }

    // One frame, painted now: what a screen refresh during the drag would do.
    void paint()
    {
        view.viewport()->grab();
    }

    // Step `at` of a drag outward from 1000 x 700, and its frame.
    void step(int at)
    {
        view.resize(1000 + 7 * at, 700 + 3 * at);
        paint();
    }

    std::size_t rasterizedCards() const
    {
        return scene.cardRenderer().rasterizations();
    }

    std::size_t paintedTables() const
    {
        return scene.tableTop()->renders();
    }
};

// Waits on the event loop, which is where the settle timer runs.
void wait(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

}

TEST_CASE("A new view is drawn sharp from its first frame", "[table]")
{
    // Nothing is frozen at the first fit: there is nothing drawn yet to scale, and
    // a first frame drawn soft would be soft for no reason.
    ShownTable table;

    REQUIRE_FALSE(table.view.isSettling());
    REQUIRE(cachedAt(table.scene, liveRatio(table.view)));
}

TEST_CASE("A single resize is drawn sharp at once and not painted again", "[table]")
{
    // Maximizing, restoring, a dock shown or hidden: one resize, and the table
    // redrawn at its new scale in the very next frame. Frozen, it would be soft
    // for 150 ms and then rasterized a second time.
    ShownTable table;

    const std::size_t tables = table.paintedTables();

    table.view.resize(1400, 900);
    table.paint();

    REQUIRE_FALSE(table.view.isSettling());
    REQUIRE(table.paintedTables() == tables + 1);
    REQUIRE(cachedAt(table.scene, liveRatio(table.view)));

    // And when the timer ends the burst, nothing is left to redo.
    const std::size_t cards = table.rasterizedCards();
    const std::size_t settled = table.paintedTables();

    wait(400);
    table.paint();

    REQUIRE(table.rasterizedCards() == cards);
    REQUIRE(table.paintedTables() == settled);
}

TEST_CASE("While a drag settles no card is rasterized again and the table is not repainted", "[table]")
{
    ShownTable table;

    // The drag's first step is drawn sharp, at full cost.
    table.step(1);
    REQUIRE_FALSE(table.view.isSettling());

    const std::size_t cards = table.rasterizedCards();
    const std::size_t tables = table.paintedTables();

    // The premise: the frames did rasterize, so the counts below are counts of
    // something.
    REQUIRE(cards > 0);
    REQUIRE(tables > 0);

    for(int step = 2 ; step <= 6 ; step++)
    {
        INFO("step " << step);

        table.step(step);

        REQUIRE(table.view.isSettling());
        REQUIRE(table.rasterizedCards() == cards);
        REQUIRE(table.paintedTables() == tables);
    }
}

// THE NAME PANELS ARE CACHED THE SAME WAY (the redesign's phase 4): each is one
// picture, its shadows blurred and its text laid out once, and a drag only scales
// what is there.
TEST_CASE("While a drag settles no panel is composed again", "[table]")
{
    ShownTable table;

    // The bid strip up, as it is while you bid: one more panel on the table, and
    // the one with the most shadows in it.
    BetPrompt prompt;
    prompt.handSize = 8;
    prompt.forbiddenBet = 3u;
    table.scene.bidStripItem()->setPrompt(prompt);

    const auto composed = [&]
    {
        std::size_t total = 0;

        for(QGraphicsItem* item : table.scene.items())
        {
            if(const auto* seat = dynamic_cast<const SeatItem*>(item))
                total += seat->compositions();

            // And the corner panels, which a drag moves but must not compose
            // again either.
            if(const auto* button = dynamic_cast<const TableButtonItem*>(item))
                total += button->compositions();

            if(const auto* scoreboard = dynamic_cast<const ScoreboardItem*>(item))
                total += scoreboard->compositions();

            if(const auto* trickPanel = dynamic_cast<const TrickPanelItem*>(item))
                total += trickPanel->compositions();

            if(const auto* strip = dynamic_cast<const BidStripItem*>(item))
                total += strip->compositions();
        }

        return total;
    };

    table.step(1);

    const std::size_t panels = composed();

    // The premise: the panels were painted, so the count below counts something.
    REQUIRE(panels > 0);

    for(int step = 2 ; step <= 6 ; step++)
    {
        INFO("step " << step);

        table.step(step);

        REQUIRE(table.view.isSettling());
        REQUIRE(composed() == panels);
    }

    // And once it settles, every panel is composed again at the new scale.
    table.view.finishResize();
    table.paint();

    REQUIRE(composed() > panels);
}

// AND THE RACKS (the redesign's phase 5), each one picture of tilted cards and a
// blurred shadow: a drag scales what they hold, and composes them once it settles.
TEST_CASE("While a drag settles no rack is composed again", "[table]")
{
    ShownTable table;

    const auto composed = [&]
    {
        std::size_t total = 0;

        for(QGraphicsItem* item : table.scene.items())
        {
            if(const auto* rack = dynamic_cast<const RackItem*>(item))
                total += rack->compositions();
        }

        return total;
    };

    table.step(1);

    const std::size_t racks = composed();

    // The premise: fullTable() gives every opponent eight cards, so five racks were
    // painted and the count below counts something.
    REQUIRE(racks >= 5);

    for(int step = 2 ; step <= 6 ; step++)
    {
        INFO("step " << step);

        table.step(step);

        REQUIRE(table.view.isSettling());
        REQUIRE(composed() == racks);
    }

    table.view.finishResize();
    table.paint();

    REQUIRE(composed() > racks);
}

TEST_CASE("While a drag settles no card's shadow is drawn again", "[table]")
{
    ShownTable table;

    table.step(1);

    const std::size_t shadows = table.scene.cardShadows().renders();

    // The premise: fullTable() has five cards down and one of them winning, and
    // eight in your hand, so the plain shadow, the winner's - the largest picture -
    // and the hand's were all drawn, and the count below counts something.
    REQUIRE(shadows >= 3);

    for(int step = 2 ; step <= 6 ; step++)
    {
        INFO("step " << step);

        table.step(step);

        REQUIRE(table.view.isSettling());
        REQUIRE(table.scene.cardShadows().renders() == shadows);
    }

    table.view.finishResize();
    table.paint();

    REQUIRE(table.scene.cardShadows().renders() > shadows);
}

TEST_CASE("While a drag settles the lead rhombus is not drawn again", "[table]")
{
    ShownTable table;

    table.step(1);

    const LeadMarkItem* mark = table.scene.leadMarkItem();
    REQUIRE(mark != nullptr);

    // The premise: fullTable() is mid-trick, so the rhombus is on the felt and was
    // drawn once at the settled scale.
    REQUIRE(mark->isVisible());

    const std::size_t drawn = mark->renders();
    REQUIRE(drawn >= 1);

    for(int step = 2 ; step <= 6 ; step++)
    {
        INFO("step " << step);

        table.step(step);

        REQUIRE(table.view.isSettling());
        REQUIRE(mark->renders() == drawn);
    }

    table.view.finishResize();
    table.paint();

    REQUIRE(mark->renders() > drawn);
}

TEST_CASE("Once a drag settles everything is rasterized at the new scale", "[table]")
{
    ShownTable table;

    table.step(1);
    table.step(2);
    REQUIRE(table.view.isSettling());

    const std::size_t cards = table.rasterizedCards();
    const std::size_t tables = table.paintedTables();

    table.view.finishResize();
    table.paint();

    REQUIRE_FALSE(table.view.isSettling());

    // Painted again, once, at the scale the window stopped at.
    REQUIRE(table.rasterizedCards() > cards);
    REQUIRE(table.paintedTables() == tables + 1);

    // AT THE LIVE SCALE, which is the point: asked for again at it, the table's
    // picture and a card from your hand are both already there.
    REQUIRE(cachedAt(table.scene, liveRatio(table.view)));
}

TEST_CASE("A drag settles by itself once the window stops changing size", "[table]")
{
    ShownTable table;

    table.step(1);
    table.step(2);
    REQUIRE(table.view.isSettling());

    // The timer's 150 ms, with room to spare.
    wait(400);

    REQUIRE_FALSE(table.view.isSettling());
}

TEST_CASE("A drag that leaves the scale alone still settles", "[table]")
{
    // A wide window is fitted by its height, so dragging its side edge keeps the
    // scale - and still asks for a room the new size at every step, unless the
    // drag is frozen like any other. Frozen at the same scale, the cards and the
    // table are drawn exactly as they were.
    ShownTable table(QSize(1800, 700));

    const qreal scale = table.view.transform().m11();

    table.view.resize(1810, 700);
    table.paint();

    const std::size_t cards = table.rasterizedCards();
    const std::size_t tables = table.paintedTables();

    table.view.resize(1820, 700);
    table.paint();

    // The premise: the width did not move the scale.
    REQUIRE(table.view.transform().m11() == scale);

    REQUIRE(table.view.isSettling());
    REQUIRE(table.rasterizedCards() == cards);
    REQUIRE(table.paintedTables() == tables);
}

TEST_CASE("Two resizes with no frame between them freeze nothing", "[table]")
{
    // A window manager's double configure, or a relayout before the first paint:
    // the scale the second resize found was never drawn, so there is nothing to
    // freeze at - only a frame that would be soft, and everything rasterized twice.
    ShownTable table;

    table.view.resize(1050, 730);
    table.view.resize(1100, 760);

    REQUIRE_FALSE(table.view.isSettling());

    // Once a frame of the burst is painted, the next resize freezes at its scale -
    // whose pixmaps are the ones in the caches, so the frame after rasterizes
    // nothing.
    table.paint();
    table.view.resize(1110, 765);
    REQUIRE(table.view.isSettling());

    const std::size_t cards = table.rasterizedCards();
    const std::size_t tables = table.paintedTables();

    table.paint();

    REQUIRE(table.rasterizedCards() == cards);
    REQUIRE(table.paintedTables() == tables);
}

TEST_CASE("A scene rendered into an image while its view settles is drawn at the image's ratio", "[table]")
{
    // The freeze is the view's viewport's. Rendered anywhere else - an export, a
    // thumbnail - the scene is drawn at the pixels of what it is rendered into.
    ShownTable table;

    table.step(1);
    table.step(2);
    REQUIRE(table.view.isSettling());

    // The fit box, one image pixel per table unit.
    const QRectF box = tablespace::fitBox;
    QImage image(QSize(qCeil(box.width()), qCeil(box.height())), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    {
        QPainter painter(&image);
        table.scene.render(&painter, QRectF(QPointF(0.0, 0.0), box.size()), box);
    }

    REQUIRE(cachedAt(table.scene, 1.0));
}

TEST_CASE("A view destroyed while it settles leaves the next view sharp", "[table]")
{
    // The freeze dies with the view. Kept on the scene, it would outlive a view
    // closed mid-drag, and every view after it would draw the table soft.
    TableScene scene;
    dealFullTable(scene);

    {
        TableView first(&scene);
        showAndPaint(first, QSize(1000, 700));

        first.resize(1010, 705);
        first.viewport()->grab();
        first.resize(1020, 710);

        REQUIRE(first.isSettling());
    }

    TableView second(&scene);
    showAndPaint(second, QSize(900, 650));

    REQUIRE_FALSE(second.isSettling());
    REQUIRE(cachedAt(scene, liveRatio(second)));
}

TEST_CASE("A view whose scene is destroyed while it settles settles safely", "[table]")
{
    // Qt drops a destroyed scene from its views, and the view asks for its scene
    // each time rather than keeping a pointer that the settle timer would then
    // reach through.
    auto scene = std::make_unique<TableScene>();
    dealFullTable(*scene);

    TableView view(scene.get());
    showAndPaint(view, QSize(1000, 700));

    view.resize(1010, 705);
    view.viewport()->grab();
    view.resize(1020, 710);
    REQUIRE(view.isSettling());

    scene.reset();
    REQUIRE(view.scene() == nullptr);

    wait(400);
    view.viewport()->grab();

    REQUIRE_FALSE(view.isSettling());
}

TEST_CASE("A settling view still paints the room into its corners", "[table]")
{
    // The room is painted at every size, settling or not - only the pixmaps on the
    // table wait - so a window grown by a drag shows no bare viewport at its new
    // edges.
    ShownTable table;

    table.step(1);
    table.view.resize(1200, 800);
    REQUIRE(table.view.isSettling());

    const QImage frame = table.view.viewport()->grab().toImage();

    for(const QPoint corner : { QPoint(0, 0), QPoint(frame.width() - 1, frame.height() - 1) })
    {
        INFO("corner " << corner.x() << ", " << corner.y());
        REQUIRE(frame.pixelColor(corner).alpha() == 255);

        // The room's edge colour, near black - not the viewport's default grey.
        REQUIRE(frame.pixelColor(corner).lightness() < 40);
    }
}
