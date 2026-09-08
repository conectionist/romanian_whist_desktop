#include <catch2/catch_test_macros.hpp>

#include "table/TableView.h"
#include "table/TableScene.h"
#include "ui/BidStrip.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QRect>
#include <QRectF>
#include <QSize>

#include <memory>

// WHERE THE STRIP LANDS, which TableSceneTests can only half answer.
//
// That file holds overlayBand() to account - the band clears the seated player's
// plaque - but the band is only the room available. This one covers the other
// half: what placeOverlay() actually does with it, which is the first thing in
// the suite to build a TableView at all.
//
// The two cases are the two ways the placement went wrong in C2. The strip is
// AS TALL AS ITS WRAPPED TEXT NEEDS, because sizeHint() answers for the width
// the strip asked for rather than the width it got, and a narrow table clipped
// the second line of the forbidden-bid reason off the bottom. And it STAYS
// INSIDE THE BAND, because clamping the y without clamping the height pushes the
// bottom edge back down over the plaque the band was computed to clear.

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

// A window of the size a person actually plays in, the same constant
// TableSceneTests measures its band at.
const QSize played(1000, 700);

// And one narrow enough to squeeze the strip below its preferred width, which is
// where the wrapping starts. A real window this wide has the scoreboard beside
// the table, so the table itself gets rather less than this.
const QSize cramped(430, 460);

// SHORTER THAN THE STRIP IS TALL, which no window a person plays in ever is -
// the band here is 92 pixels and the wrapped strip wants 106. It is in the list
// because it is the only size at which the height clamp is observable: every
// playable table has room to spare, so a clamp that was missing would sit unseen
// until the day something else made the strip taller.
const QSize squashed(400, 120);

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

        const QRectF band = scene.overlayBand();

        REQUIRE_FALSE(band.isEmpty());

        // BOTH EDGES. The bottom one is the plaque the band exists to clear; the
        // top one is the clamp that keeps a tall strip from being pushed back
        // down through it.
        REQUIRE(strip.geometry().bottom() <= static_cast<int>(band.bottom()));
        REQUIRE(strip.geometry().top() >= static_cast<int>(band.top()));
    }
}
