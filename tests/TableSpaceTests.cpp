#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "table/SeatItem.h"
#include "table/TableSpace.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <cstddef>

// THE TABLE REDESIGN'S COORDINATES (section 2.1), and the two questions about
// them with a right answer: how much the window scales them by, and which card
// size a seat count plays at. Everything else in TableSpace.h is a number copied
// from the mock-up, and a test that repeated the number would only check the copy.

namespace
{

// The box and its margins at a scale of exactly one: 1880 + 16 + 16 across, and
// 1324 + 10 + 8 down.
const QSizeF unscaled(1912.0, 1342.0);

}

TEST_CASE("The fit scale is one at the box's own size and grows with the window", "[table-space]")
{
    REQUIRE(tablespace::fitScale(unscaled) == Catch::Approx(1.0));

    // Twice the room inside the margins is twice the scale - which is the margins
    // staying in device pixels rather than scaling with the table.
    REQUIRE(tablespace::fitScale(QSizeF(1880.0 * 2.0 + 32.0, 1324.0 * 2.0 + 18.0))
            == Catch::Approx(2.0));
}

// ONE SCALE FOR BOTH AXES, the smaller of the two, so the table stays round and
// the whole box fits. A window much wider than the box's aspect is limited by its
// height, and one much taller by its width; asking each question of the other
// window would get a scale that pushed the box off the screen.
TEST_CASE("The fit scale is the tighter of the two axes", "[table-space]")
{
    // Height-limited: wide and short.
    REQUIRE(tablespace::fitScale(QSizeF(4000.0, 1324.0 / 2.0 + 18.0)) == Catch::Approx(0.5));

    // Width-limited: narrow and tall.
    REQUIRE(tablespace::fitScale(QSizeF(1880.0 / 2.0 + 32.0, 4000.0)) == Catch::Approx(0.5));
}

TEST_CASE("A viewport with no room inside its margins has no fit scale", "[table-space]")
{
    // Zero rather than a negative or a division by zero. TableView reads it as
    // "leave the transform alone", which is what a window being created needs.
    REQUIRE(tablespace::fitScale(QSizeF()) == 0.0);
    REQUIRE(tablespace::fitScale(QSizeF(32.0, 500.0)) == 0.0);
    REQUIRE(tablespace::fitScale(QSizeF(500.0, 18.0)) == 0.0);
}

// The fit box is centred on the table. That is what lets TableView centre the
// box, by its alignment, and have centred the table - so the day someone widens
// the box on one side only, this fails rather than the table drifting off centre.
TEST_CASE("The fit box is centred on the table", "[table-space]")
{
    REQUIRE(tablespace::fitBox.center() == tablespace::centre);
    REQUIRE(tablespace::fitBox.contains(QRectF(QPointF(0.0, 0.0), tablespace::stage)));
}

// THE FLOOR TableView's minimum size holds: a viewport of minimumViewport() fits
// the box at minimumScale or more, and a pixel less in either direction would not.
TEST_CASE("The minimum viewport fits the table at the minimum scale", "[table-space]")
{
    const QSize minimum = tablespace::minimumViewport();

    REQUIRE(minimum == QSize(709, 495));

    REQUIRE(tablespace::fitScale(minimum) >= tablespace::minimumScale - 1e-9);
    REQUIRE(tablespace::fitScale(minimum - QSize(1, 0)) < tablespace::minimumScale);
    REQUIRE(tablespace::fitScale(minimum - QSize(0, 1)) < tablespace::minimumScale);
}

// WHAT THE FLOOR IS FOR: a panel's name stays at nine pixels or more at the
// smallest scale. A smaller name font, or a lower floor, fails here by name
// rather than going unnoticed at the smallest window.
TEST_CASE("The minimum scale keeps a panel's name at nine pixels", "[table-space]")
{
    REQUIRE(seatitem::nameSize * tablespace::minimumScale >= 9.0 - 1e-9);
}

// Section 2.7: the larger card up to four seats, the smaller from five.
TEST_CASE("Played cards are the larger size up to four seats", "[table-space]")
{
    for(std::size_t seats = 2 ; seats <= 4 ; seats++)
        REQUIRE(tablespace::playedCardHeight(seats) == Catch::Approx(205.2));

    for(std::size_t seats = 5 ; seats <= 6 ; seats++)
        REQUIRE(tablespace::playedCardHeight(seats) == Catch::Approx(181.2));
}
