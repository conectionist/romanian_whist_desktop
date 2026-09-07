#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "table/SeatLayout.h"

#include <QPointF>
#include <QRectF>

#include <string>
#include <vector>

#include <cmath>

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
        const qreal expected = 2.0 * radius * std::sin(M_PI / seatCount);

        for(unsigned int seat = 0 ; seat < seatCount ; seat++)
        {
            const QPointF step = ring[(seat + 1) % seatCount] - ring[seat];

            REQUIRE(std::hypot(step.x(), step.y()) == Catch::Approx(expected));
        }
    }

    REQUIRE(seatlayout::ringRect(centre, 0.0).isEmpty());
}
