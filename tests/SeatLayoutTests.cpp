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
#include <numbers>

// Section 14's seat table, asserted as the WORDS IT IS WRITTEN IN rather than
// against the coordinates the implementation produces.
//
// That is the whole design of this file. The plan spells the placements out as
// "human bottom; bots left, top, right" and the implementation turns all five
// rows into one formula; a test that checked the formula's own output would
// only prove the formula equals itself. Classifying each centre back into a
// side and comparing it to the plan's phrasing is what makes this an
// independent reading of section 14.
namespace
{

// The rect every case is laid in. Deliberately not square and not at the
// origin: a layout that assumed either would pass on one that was.
const QRectF table(40.0, 90.0, 800.0, 600.0);

// Which of the eight compass points a centre sits at, relative to the rect's
// middle. The tolerance is what makes "bottom" mean bottom-CENTRE rather than
// "somewhere below" - at 180 degrees sin() is 1.2e-16 rather than 0, and a real
// off-centre seat is a whole radius away, so anything in between is a bug this
// should catch rather than round away.
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

std::vector<Side> sidesFor(unsigned int seatCount, int humanSeat)
{
    std::vector<Side> sides;

    for(const QPointF& centre : seatlayout::seatCentres(seatCount, humanSeat, table))
        sides.push_back(sideOf(centre, table));

    return sides;
}

}

// Each row read straight off section 14. The human is seat 0 here so that the
// vector reads in the same order the plan writes it.
TEST_CASE("Section 14's seat table, row by row", "[seat-layout]")
{
    SECTION("2 - human bottom; bot top")
    {
        REQUIRE(sidesFor(2, 0) == std::vector<Side>{ Side::Bottom, Side::Top });
    }

    SECTION("3 - human bottom; bots top-left, top-right")
    {
        REQUIRE(sidesFor(3, 0) == std::vector<Side>{ Side::Bottom, Side::TopLeft, Side::TopRight });
    }

    SECTION("4 - human bottom; bots left, top, right")
    {
        REQUIRE(sidesFor(4, 0)
                == std::vector<Side>{ Side::Bottom, Side::Left, Side::Top, Side::Right });
    }

    SECTION("5 - human bottom; bots bottom-left, top-left, top-right, bottom-right")
    {
        REQUIRE(sidesFor(5, 0)
                == std::vector<Side>{ Side::Bottom, Side::BottomLeft, Side::TopLeft,
                                      Side::TopRight, Side::BottomRight });
    }

    SECTION("6 - human bottom; bots bottom-left, top-left, top, top-right, bottom-right")
    {
        REQUIRE(sidesFor(6, 0)
                == std::vector<Side>{ Side::Bottom, Side::BottomLeft, Side::TopLeft, Side::Top,
                                      Side::TopRight, Side::BottomRight });
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

            const std::vector<QPointF> centres =
                seatlayout::seatCentres(seatCount, static_cast<int>(human), table);

            REQUIRE(centres.size() == seatCount);
            REQUIRE(sideOf(centres[human], table) == Side::Bottom);
        }
    }
}

// "In seat order, CLOCKWISE, so the play order on screen matches the play order
// in the game." A sign flip on the x term mirrors the table: every row above
// still passes for 2 and 4 seats, where the placements are symmetric, and 3, 5
// and 6 come out reversed. This asserts the direction itself.
TEST_CASE("Seats run clockwise from the human", "[seat-layout]")
{
    // The seat that plays after the human is on the LEFT of the table, because
    // bottom -> left is clockwise on a screen whose y runs downward.
    for(unsigned int seatCount = 3 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);

        const std::vector<QPointF> centres = seatlayout::seatCentres(seatCount, 0, table);

        REQUIRE(centres[1].x() < table.center().x());
        REQUIRE(centres[seatCount - 1].x() > table.center().x());
    }

    // And it is the human's NEIGHBOUR that goes left, not seat 1 - which is the
    // same assertion once the human is not seat 0.
    const std::vector<QPointF> rotated = seatlayout::seatCentres(4, 2, table);

    REQUIRE(sideOf(rotated[2], table) == Side::Bottom);
    REQUIRE(sideOf(rotated[3], table) == Side::Left);
    REQUIRE(sideOf(rotated[0], table) == Side::Top);
    REQUIRE(sideOf(rotated[1], table) == Side::Right);
}

// IN FRACTIONS OF THE RECT, which is section 14's rule and the reason the
// background stays replaceable. A hardcoded pixel radius passes every test
// above, because they all use one rect.
TEST_CASE("Seat centres are fractions of the rect they are laid in", "[seat-layout]")
{
    const QRectF doubled(table.x(), table.y(), table.width() * 2.0, table.height() * 2.0);

    const std::vector<QPointF> small = seatlayout::seatCentres(4, 0, table);
    const std::vector<QPointF> large = seatlayout::seatCentres(4, 0, doubled);

    // Twice the rect, twice the offset from its centre.
    for(unsigned int seat = 0 ; seat < 4 ; seat++)
    {
        INFO("seat " << seat);

        const QPointF smallOffset = small[seat] - table.center();
        const QPointF largeOffset = large[seat] - doubled.center();

        REQUIRE(largeOffset.x() == Catch::Approx(smallOffset.x() * 2.0));
        REQUIRE(largeOffset.y() == Catch::Approx(smallOffset.y() * 2.0));
    }

    // And a moved rect moves the seats with it, rather than laying them out
    // around the origin.
    const QRectF moved = table.translated(1000.0, 500.0);

    const std::vector<QPointF> shifted = seatlayout::seatCentres(4, 0, moved);

    for(unsigned int seat = 0 ; seat < 4 ; seat++)
    {
        INFO("seat " << seat);

        REQUIRE(shifted[seat].x() == Catch::Approx(small[seat].x() + 1000.0));
        REQUIRE(shifted[seat].y() == Catch::Approx(small[seat].y() + 500.0));
    }

    // Every seat inside the rect, at every size the table supports. A radius
    // fraction that grew past the edge would put a plaque off screen.
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(const QPointF& centre : seatlayout::seatCentres(seatCount, 0, table))
        {
            INFO("seatCount " << seatCount);
            REQUIRE(table.contains(centre));
        }
    }
}

// A spectator game has no human, and section 14 distributes every seat around
// the full ellipse - which is what this already does with seat 0 at the bottom.
// One code path, and this is what says so.
TEST_CASE("A spectator game lays seats out as though seat 0 were the human", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);
        REQUIRE(seatlayout::seatCentres(seatCount, -1, table)
                == seatlayout::seatCentres(seatCount, 0, table));
    }
}

// Two seats at one point is a table with a card stack drawn on top of another,
// and it is what a modulo that wraps early produces.
TEST_CASE("No two seats share a centre", "[seat-layout]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        const std::vector<QPointF> centres = seatlayout::seatCentres(seatCount, 0, table);

        for(std::size_t left = 0 ; left < centres.size() ; left++)
        {
            for(std::size_t right = left + 1 ; right < centres.size() ; right++)
            {
                INFO("seatCount " << seatCount << ", seats " << left << " and " << right);
                REQUIRE(centres[left] != centres[right]);
            }
        }
    }
}

TEST_CASE("Degenerate requests produce no seats rather than a division by zero", "[seat-layout]")
{
    REQUIRE(seatlayout::seatCentres(0, -1, table).empty());
    REQUIRE(seatlayout::seatCentres(4, 0, QRectF()).empty());

    // A widget's first layout pass, before it has been given a size.
    REQUIRE(seatlayout::seatCentres(4, 0, QRectF(0.0, 0.0, 0.0, 400.0)).empty());

    // A human seat that does not exist at this table is read as "nobody" rather
    // than wrapped into one that does - a rotated table would hide the caller's
    // bug behind a layout that merely looks odd.
    REQUIRE(seatlayout::seatCentres(4, 9, table) == seatlayout::seatCentres(4, -1, table));
}

// ringRect() is the inverse of the radius fractions, and it exists so the TRICK
// can be laid out by the same function as the seats over a rect it chose. What
// it has to guarantee is the thing an ellipse does not: that neighbouring
// positions are all the same distance apart, whatever shape the window is.
TEST_CASE("A ring rect lays every seat the same distance from the centre", "[seat-layout]")
{
    const QPointF centre(500.0, 300.0);
    const qreal radius = 120.0;

    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);

        const std::vector<QPointF> ring =
            seatlayout::seatCentres(seatCount, 0, seatlayout::ringRect(centre, radius));

        REQUIRE(ring.size() == seatCount);

        for(const QPointF& point : ring)
        {
            const QPointF offset = point - centre;
            REQUIRE(std::hypot(offset.x(), offset.y()) == Catch::Approx(radius));
        }
    }
}

// THE PROPERTY THE TRICK ACTUALLY NEEDS, and the one the first draft of the
// scene did not have: neighbours evenly spaced, so a ring sized to clear one
// card clears all of them. Laid along the SEAT ellipse instead, six seats put
// two cards in the same place on a window wider than it is tall.
TEST_CASE("A ring rect spaces neighbours evenly", "[seat-layout]")
{
    const QPointF centre(500.0, 300.0);
    const qreal radius = 120.0;

    for(unsigned int seatCount = 3 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);

        const std::vector<QPointF> ring =
            seatlayout::seatCentres(seatCount, 0, seatlayout::ringRect(centre, radius));

        // The chord between adjacent seats on a circle: 2 r sin(pi/n).
        const qreal expected = 2.0 * radius * std::sin(std::numbers::pi / seatCount);

        for(unsigned int seat = 0 ; seat < seatCount ; seat++)
        {
            const QPointF step = ring[(seat + 1) % seatCount] - ring[seat];

            REQUIRE(std::hypot(step.x(), step.y()) == Catch::Approx(expected));
        }
    }

    REQUIRE(seatlayout::ringRect(centre, 0.0).isEmpty());
}

// THE TWO-RADIUS FORM, added in the table redesign's phase 2 for the interim seat
// ring: a seat straight below or above the centre sits at the vertical radius
// and one straight beside it at the horizontal one - which is what "an ellipse of
// exactly those radii" means, and what dividing each radius by its OWN fraction
// buys. Four seats are the count that puts a seat on each of the four axes.
TEST_CASE("A two-radius ring rect lays an ellipse of exactly those radii", "[seat-layout]")
{
    const QPointF centre(768.0, 512.0);
    const qreal radiusX = 662.0;
    const qreal radiusY = 593.0;

    const std::vector<QPointF> ring =
        seatlayout::seatCentres(4, 0, seatlayout::ringRect(centre, radiusX, radiusY));

    REQUIRE(ring.size() == 4);

    // Bottom, left, top, right: clockwise from seat 0 at the bottom.
    REQUIRE(ring[0].x() == Catch::Approx(centre.x()));
    REQUIRE(ring[0].y() == Catch::Approx(centre.y() + radiusY));
    REQUIRE(ring[1].x() == Catch::Approx(centre.x() - radiusX));
    REQUIRE(ring[1].y() == Catch::Approx(centre.y()));
    REQUIRE(ring[2].y() == Catch::Approx(centre.y() - radiusY));
    REQUIRE(ring[3].x() == Catch::Approx(centre.x() + radiusX));

    // Equal radii are the one-radius form, so the circle above is a special case
    // of this and not a second implementation of it.
    REQUIRE(seatlayout::ringRect(centre, 120.0, 120.0) == seatlayout::ringRect(centre, 120.0));

    REQUIRE(seatlayout::ringRect(centre, 120.0, 0.0).isEmpty());
}

// ---- the name panels, added in the table redesign's phase 4 ----
//
// Section 2.5 places every panel by hand, per player count. The table is copied
// verbatim, and the first case below checks the copy; the rest check what the
// hand-tuning was FOR - the same order as the ring, clear of the table, clear of
// each other - so that a later edit to one number that breaks one of those fails
// by name rather than by looking slightly off in a screenshot.

namespace
{

// The stage, whose centre is the table's: what a panel's side is read against.
const QRectF stage(QPointF(0.0, 0.0), tablespace::stage);

std::vector<QRectF> panelsFor(unsigned int seatCount, int humanSeat)
{
    std::vector<QRectF> panels;

    for(unsigned int seat = 0 ; seat < seatCount ; seat++)
        panels.push_back(seatlayout::panelRect(seatCount, humanSeat, seat));

    return panels;
}

// How far the nearest point of `rect` is from the table's centre.
qreal clearance(const QRectF& rect)
{
    const QPointF centre = tablespace::centre;
    const qreal x = std::clamp(centre.x(), rect.left(), rect.right());
    const qreal y = std::clamp(centre.y(), rect.top(), rect.bottom());

    return std::hypot(x - centre.x(), y - centre.y());
}

}

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

TEST_CASE("The panels go round the table in the ring's order", "[seat-layout]")
{
    // SECTION 14'S ROWS AGAIN, read off the panels: each panel sits on the side of
    // the table its seat's ring position does - whichever seat the human has, and
    // in a spectator game. The ring's own cases above check those rows against the
    // plan's words, so this ties the hand-tuned table to the same words.
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(int humanSeat = -1 ; humanSeat < static_cast<int>(seatCount) ; humanSeat++)
        {
            INFO("seatCount " << seatCount << ", humanSeat " << humanSeat);

            std::vector<Side> sides;

            for(const QRectF& panel : panelsFor(seatCount, humanSeat))
                sides.push_back(sideOf(panel.center(), stage));

            REQUIRE(sides == sidesFor(seatCount, humanSeat));
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
