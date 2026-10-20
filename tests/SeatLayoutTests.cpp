#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "table/SeatLayout.h"
#include "table/TableSpace.h"

#include <QPointF>
#include <QRectF>

#include <string>
#include <vector>

#include <algorithm>
#include <cmath>

// Section 14's seat table, asserted as the WORDS IT IS WRITTEN IN rather than
// against the coordinates the implementation produces.
//
// That is the whole design of the first half of this file. The plan spells the
// placements out as "human bottom; bots left, top, right"; a test that checked
// the panels' own coordinates would only prove the table equals itself.
// Classifying each panel's centre back into a side and comparing it to the plan's
// phrasing is what makes this an independent reading of section 14.
//
// THE PANELS ARE WHAT IS READ, since the redesign's phase 6. Until then the seats
// had a ring of their own, seatlayout::seatCentres(), which these cases read and
// which the panels were checked against; the played cards were its last user.
namespace
{

// The stage, whose centre is the table's: what a panel's side is read against.
const QRectF stage(QPointF(0.0, 0.0), tablespace::stage);

// Which of the eight compass points a centre sits at, relative to the rect's
// middle. The tolerance is what makes "bottom" mean bottom-CENTRE rather than
// "somewhere below": a real off-centre panel is hundreds of units out, so
// anything in between is a bug this should catch rather than round away.
enum class Side
{
    Bottom,
    BottomLeft,
    Left,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight
};

Side sideOf(QPointF centre, QRectF rect)
{
    const qreal toleranceX = rect.width() * 0.01;
    const qreal toleranceY = rect.height() * 0.01;

    const qreal dx = centre.x() - rect.center().x();
    const qreal dy = centre.y() - rect.center().y();

    const bool centredX = std::abs(dx) < toleranceX;
    const bool centredY = std::abs(dy) < toleranceY;

    if(centredX)
        return dy > 0 ? Side::Bottom : Side::Top;

    if(centredY)
        return dx < 0 ? Side::Left : Side::Right;

    if(dy > 0)
        return dx < 0 ? Side::BottomLeft : Side::BottomRight;

    return dx < 0 ? Side::TopLeft : Side::TopRight;
}

std::vector<QRectF> panelsFor(unsigned int seatCount, int humanSeat)
{
    std::vector<QRectF> panels;

    for(unsigned int seat = 0 ; seat < seatCount ; seat++)
        panels.push_back(seatlayout::panelRect(seatCount, humanSeat, seat));

    return panels;
}

std::vector<Side> sidesFor(unsigned int seatCount, int humanSeat)
{
    std::vector<Side> sides;

    for(const QRectF& panel : panelsFor(seatCount, humanSeat))
        sides.push_back(sideOf(panel.center(), stage));

    return sides;
}

// Section 14's rows, in the plan's words, from the bottom seat clockwise.
const std::vector<std::vector<Side>> rows = {
    { Side::Bottom, Side::Top },
    { Side::Bottom, Side::TopLeft, Side::TopRight },
    { Side::Bottom, Side::Left, Side::Top, Side::Right },
    { Side::Bottom, Side::BottomLeft, Side::TopLeft, Side::TopRight, Side::BottomRight },
    { Side::Bottom, Side::BottomLeft, Side::TopLeft, Side::Top, Side::TopRight, Side::BottomRight }
};

// How far the nearest point of `rect` is from the table's centre.
qreal clearance(const QRectF& rect)
{
    const QPointF centre = tablespace::centre;
    const qreal x = std::clamp(centre.x(), rect.left(), rect.right());
    const qreal y = std::clamp(centre.y(), rect.top(), rect.bottom());

    return std::hypot(x - centre.x(), y - centre.y());
}

}

// Each row read straight off section 14. The human is seat 0 here so that the
// vector reads in the same order the plan writes it.
TEST_CASE("Section 14's seat table, row by row", "[seat-layout]")
{
    SECTION("2 - human bottom; bot top")
    {
        REQUIRE(sidesFor(2, 0) == rows[0]);
    }

    SECTION("3 - human bottom; bots top-left, top-right")
    {
        REQUIRE(sidesFor(3, 0) == rows[1]);
    }

    SECTION("4 - human bottom; bots left, top, right")
    {
        REQUIRE(sidesFor(4, 0) == rows[2]);
    }

    SECTION("5 - human bottom; bots bottom-left, top-left, top-right, bottom-right")
    {
        REQUIRE(sidesFor(5, 0) == rows[3]);
    }

    SECTION("6 - human bottom; bots bottom-left, top-left, top, top-right, bottom-right")
    {
        REQUIRE(sidesFor(6, 0) == rows[4]);
    }
}

// "The human is always at bottom-centre" - section 14's first sentence, and the
// one thing true of every table size. An offset that ignored humanSeat would
// pass the row above, where the human happens to be seat 0.
TEST_CASE("The human seat is at bottom-centre whichever seat it is", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(unsigned int human = 0 ; human < seatCount ; human++)
        {
            INFO("seatCount " << seatCount << ", human " << human);

            const QRectF panel = seatlayout::panelRect(seatCount, static_cast<int>(human), human);

            REQUIRE(panel == tablespace::bottomPanel);
            REQUIRE(sideOf(panel.center(), stage) == Side::Bottom);
        }
    }
}

// "In seat order, CLOCKWISE, so the play order on screen matches the play order
// in the game." A table mirrored left to right passes the rows for 2 and 4 seats,
// where the placements are symmetric, and gets 3, 5 and 6 reversed. This asserts
// the direction itself.
TEST_CASE("Seats run clockwise from the human", "[seat-layout]")
{
    // The seat that plays after the human is on the LEFT of the table, because
    // bottom -> left is clockwise on a screen whose y runs downward.
    for(unsigned int seatCount = 3 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);

        const std::vector<QRectF> panels = panelsFor(seatCount, 0);

        REQUIRE(panels[1].center().x() < stage.center().x());
        REQUIRE(panels[seatCount - 1].center().x() > stage.center().x());
    }

    // And it is the human's NEIGHBOUR that goes left, not seat 1 - which is the
    // same assertion once the human is not seat 0.
    const std::vector<Side> rotated = sidesFor(4, 2);

    REQUIRE(rotated[2] == Side::Bottom);
    REQUIRE(rotated[3] == Side::Left);
    REQUIRE(rotated[0] == Side::Top);
    REQUIRE(rotated[1] == Side::Right);
}

// SECTION 14'S ROWS WHOEVER SITS AT THE BOTTOM: each seat's panel is on the side
// its place clockwise from the bottom says, for every seat the human can have and
// in a spectator game.
TEST_CASE("The panels keep section 14's rows whoever is at the bottom", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(int humanSeat = -1 ; humanSeat < static_cast<int>(seatCount) ; humanSeat++)
        {
            INFO("seatCount " << seatCount << ", humanSeat " << humanSeat);

            const std::vector<Side> sides = sidesFor(seatCount, humanSeat);
            const unsigned int bottom = humanSeat < 0 ? 0 : static_cast<unsigned int>(humanSeat);

            for(unsigned int step = 0 ; step < seatCount ; step++)
            {
                INFO("step " << step);
                REQUIRE(sides[(bottom + step) % seatCount] == rows[seatCount - 2][step]);
            }
        }
    }
}

// A spectator game has no human, and section 14 lays its table out as though seat
// 0 were the human. One code path, and this is what says so.
TEST_CASE("A spectator game lays seats out as though seat 0 were the human", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);
        REQUIRE(panelsFor(seatCount, -1) == panelsFor(seatCount, 0));
    }

    // A human seat that does not exist at this table is read as "nobody" rather
    // than wrapped into one that does - a rotated table would hide the caller's
    // bug behind a layout that merely looks odd.
    REQUIRE(panelsFor(4, 9) == panelsFor(4, -1));
}

// ---- the name panels, added in the table redesign's phase 4 ----
//
// Section 2.5 places every panel by hand, per player count. The table is copied
// verbatim, and the first case below checks the copy; the rest check what the
// hand-tuning was FOR - clear of the table, clear of each other - so that a later
// edit to one number that breaks one of those fails by name rather than by
// looking slightly off in a screenshot.

TEST_CASE("The panels are section 2.5's table", "[seat-layout]")
{
    // Top-left corners from the first seat clockwise of the bottom one; the
    // bottom seat itself is the wider panel centred under the table.
    const std::vector<std::vector<QPointF>> corners = {
        { { 628, -140 } },
        { { 96, 49 }, { 1160, 49 } },
        { { -34, 453 }, { 628, -140 }, { 1290, 453 } },
        { { -24, 611 }, { 96, 49 }, { 1160, 49 }, { 1280, 611 } },
        { { 1, 700 }, { 96, 49 }, { 628, -140 }, { 1160, 49 }, { 1255, 700 } }
    };

    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);

        const std::vector<QRectF> panels = panelsFor(seatCount, 0);

        REQUIRE(panels.front() == QRectF(618, 1046, 300, 118));
        REQUIRE(panels.front().center() == QPointF(768, 1105));

        for(unsigned int seat = 1 ; seat < seatCount ; seat++)
        {
            INFO("seat " << seat);
            REQUIRE(panels[seat] == QRectF(corners[seatCount - 2][seat - 1], QSizeF(280, 118)));
        }
    }
}

TEST_CASE("Every panel sits outside the wooden rim", "[seat-layout]")
{
    // THE RIM IS 510, and every panel's nearest point is at least 10 past it,
    // which leaves the opponents' racks (phase 5) their strip of rim. The plan's
    // section 4 says 522; the five-player bottom panels' square corners come to
    // 521.5, and only their rounded ones clear 522 - so this asserts what the
    // mock-up's numbers actually give.
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(const QRectF& panel : panelsFor(seatCount, 0))
        {
            INFO("seatCount " << seatCount << ", panel at " << panel.x() << ", " << panel.y());
            REQUIRE(clearance(panel) >= tablespace::woodRadius + 10.0);
        }
    }
}

TEST_CASE("No two panels overlap and all fit the box", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        const std::vector<QRectF> panels = panelsFor(seatCount, 0);

        for(std::size_t left = 0 ; left < panels.size() ; left++)
        {
            INFO("seatCount " << seatCount << ", seat " << left);
            REQUIRE(tablespace::fitBox.contains(panels[left]));

            for(std::size_t right = left + 1 ; right < panels.size() ; right++)
            {
                INFO("and seat " << right);
                REQUIRE_FALSE(panels[left].intersects(panels[right]));
            }
        }
    }
}

TEST_CASE("A table the panels have no row for gets no panels", "[seat-layout]")
{
    // Section 2.5 runs from two to six. Anything else is not a table to invent a
    // layout for, and a seat that is not at the table has no place at it.
    REQUIRE(seatlayout::panelRect(0, 0, 0).isEmpty());
    REQUIRE(seatlayout::panelRect(1, 0, 0).isEmpty());
    REQUIRE(seatlayout::panelRect(7, 0, 0).isEmpty());
    REQUIRE(seatlayout::panelRect(4, 0, 4).isEmpty());
}

TEST_CASE("Somebody is seated only at a seat that is at the table", "[seat-layout]")
{
    REQUIRE(seatlayout::humanSeated(4, 0));
    REQUIRE(seatlayout::humanSeated(4, 3));

    // A spectator game, and an index past the table, read as nobody.
    REQUIRE_FALSE(seatlayout::humanSeated(4, -1));
    REQUIRE_FALSE(seatlayout::humanSeated(4, 4));
}

TEST_CASE("A seat's place is its panel, and its rack's angle unless it is yours", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(const int humanSeat : { -1, 0, 1 })
        {
            const seatlayout::Places all = seatlayout::places(seatCount, humanSeat);
            REQUIRE(all.size() == seatCount);

            for(unsigned int seat = 0 ; seat < seatCount ; seat++)
            {
                INFO("seatCount " << seatCount << ", humanSeat " << humanSeat << ", seat " << seat);

                REQUIRE(all[seat].has_value());

                const seatlayout::Place& place = *all[seat];
                const bool yours = humanSeat == static_cast<int>(seat);

                REQUIRE(place.panel == seatlayout::panelRect(seatCount, humanSeat, seat));
                REQUIRE(place.human == yours);

                // Yours has no rack, so no angle - not a 0 that is also the
                // right-hand seat's (PHASE5_REVIEW_FINDINGS.md #16).
                if(yours)
                    REQUIRE_FALSE(place.angle.has_value());
                else
                    REQUIRE(place.angle == seatlayout::rackAngle(place.panel));
            }
        }
    }

    // None for a seat with no panel.
    REQUIRE_FALSE(seatlayout::place(4, 0, 4));
    REQUIRE_FALSE(seatlayout::place(7, 0, 1));
}

// PHASE5_REVIEW_FINDINGS.md #16: an empty panel's angle was 0, the right-hand seat's
// real one, so "no rack" read as a rack to the right.
TEST_CASE("An empty panel has no rack angle", "[seat-layout]")
{
    REQUIRE_FALSE(seatlayout::rackAngle(QRectF()).has_value());
    REQUIRE(seatlayout::rackAngle(seatlayout::panelRect(4, 0, 3)).has_value());
}
