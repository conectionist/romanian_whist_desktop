#include <catch2/catch_test_macros.hpp>

#include "table/TableView.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"
#include "ui/BidStrip.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QGraphicsItem>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>

#include <cmath>
#include <map>
#include <memory>

// WHERE THE STRIP LANDS, which TableSceneTests can only half answer.
//
// That file holds overlayBand() to account - the band clears your hand and your
// plaque - but the band is only the room available, and in table space besides.
// This one covers the other half: what placeOverlay() actually does with it once
// the view has mapped it to the screen.
//
// The two strip cases are the two ways the placement went wrong in C2. The strip
// is AS TALL AS ITS WRAPPED TEXT NEEDS, because sizeHint() answers for the width
// the strip asked for rather than the width it got, and a narrow table clipped
// the second line of the forbidden-bid reason off the bottom. And it STAYS INSIDE
// THE BAND, because clamping the y without clamping the height pushes the bottom
// edge back down over the plaque the band was computed to clear.
//
// AND THE SCALE ITSELF, since the table redesign's phase 2: the view fits a fixed
// table to the window, centred, and a resize changes that scale and nothing in
// the scene.

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

// WITH A FORBIDDEN BID, so the explanation label is not empty. It is the only
// part of the strip that wraps, so a prompt without one cannot show the clipping
// this file is about.
BetPrompt lastBidder()
{
    BetPrompt prompt;
    prompt.promptId = 7;
    prompt.handSize = 8;
    prompt.forbiddenBet = 3u;

    return prompt;
}

// A window of the size a person actually plays in.
const QSize played(1000, 700);

// And one narrow enough to squeeze the strip below its preferred width, which is
// where the wrapping starts. A real window this wide has the scoreboard beside
// the table, so the table itself gets rather less than this.
const QSize cramped(430, 460);

// SHORTER THAN THE STRIP IS TALL, which no window a person plays in ever is. It
// is in the list because it is the only size at which the height clamp is
// observable: every playable table has room to spare, so a clamp that was
// missing would sit unseen until the day something else made the strip taller.
const QSize squashed(400, 120);

// The band, where the view puts it: the viewport's pixels, in the view's own
// coordinates, which is where the strip - the view's child - is placed.
QRect bandOnScreen(const TableView& view, const TableScene& scene)
{
    return view.mapFromScene(scene.overlayBand()).boundingRect()
        .translated(view.viewport()->geometry().topLeft());
}

// Windows of every shape the scale has to handle: the screenshot size, a laptop,
// the size the suite plays at, an ultra-wide, and one taller than it is wide.
const QSize shapes[] = { QSize(1920, 1080), QSize(1440, 900), QSize(1000, 700),
                         QSize(2560, 1080), QSize(800, 1200) };

}

// The strip is on the stack in both cases and DECLARED AFTER THE VIEW, which is
// load-bearing: setBottomOverlay() reparents it, so a view destroyed first would
// delete a stack object. Reverse order means the strip leaves the parent before
// the parent goes.

TEST_CASE("A squeezed strip is as tall as its wrapped explanation needs", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    TableView view(&scene);
    BidStrip strip;

    strip.setPrompt(lastBidder());

    view.setBottomOverlay(&strip);
    view.resize(cramped);
    view.show();

    // The premise of the case, asserted rather than assumed: at this width the
    // strip is narrower than it asked to be, which is what makes the wrapped
    // label taller than sizeHint() says.
    REQUIRE(strip.width() < strip.sizeHint().width());
    REQUIRE(strip.hasHeightForWidth());

    // AGAINST heightForWidth RATHER THAN A NUMBER. What the sentence needs
    // depends on the font the test machine has, so a pixel literal would assert
    // the font. The relation does not: at the width it was given, the strip must
    // have at least the height that width implies.
    REQUIRE(strip.heightForWidth(strip.width()) > strip.sizeHint().height());
    REQUIRE(strip.height() >= strip.heightForWidth(strip.width()));
}

TEST_CASE("The strip stays inside the band at every size", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    TableView view(&scene);
    BidStrip strip;

    strip.setPrompt(lastBidder());

    view.setBottomOverlay(&strip);
    view.show();

    for(const QSize& size : {played, cramped, squashed})
    {
        INFO("table " << size.width() << "x" << size.height());

        view.resize(size);

        const QRect band = bandOnScreen(view, scene);

        REQUIRE_FALSE(band.isEmpty());

        // BOTH EDGES. The bottom one is the hand and the plaque the band exists to
        // clear; the top one is the clamp that keeps a tall strip from being
        // pushed back down through them.
        REQUIRE(strip.geometry().bottom() <= band.bottom());
        REQUIRE(strip.geometry().top() >= band.top());
    }
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

        // AND CENTRED: the table's own centre lands on the viewport's, to within
        // the pixel the 10-and-8 vertical margins leave between them.
        const QPoint centre = view.mapFromScene(tablespace::centre);

        REQUIRE(std::abs(centre.x() - viewport.width() / 2.0) <= 1.0);
        REQUIRE(std::abs(centre.y() - viewport.height() / 2.0) <= 1.5);
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

        for(const QGraphicsItem* item : scene.items())
            REQUIRE(item->sceneBoundingRect() == before.at(item));
    }
}
