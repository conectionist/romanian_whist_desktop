#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "table/Motion.h"
#include "table/TableScene.h"

#include <QEasingCurve>

using Catch::Approx;

// HOW MUCH THE TABLE MOVES (the table redesign's phase 12): the setting's four
// stops, what each takes, and the two curves. Pure numbers, so no scene.

TEST_CASE("The slider's stops are the paces, left to right", "[motion]")
{
    using motion::Pace;

    REQUIRE(motion::stopCount == 4);
    REQUIRE(motion::stopOf(Pace::Off) == 0);
    REQUIRE(motion::stopOf(Pace::Quick) == 1);
    REQUIRE(motion::stopOf(Pace::Normal) == 2);
    REQUIRE(motion::stopOf(Pace::Slow) == 3);

    for(int stop = 0 ; stop < motion::stopCount ; stop++)
        REQUIRE(motion::stopOf(motion::paceAt(stop)) == stop);

    // The mock-up's lengths are what a player gets until they ask for others.
    REQUIRE(motion::defaultPace == Pace::Normal);
}

TEST_CASE("A stop out of range is the nearest pace", "[motion]")
{
    REQUIRE(motion::paceAt(-3) == motion::Pace::Off);
    REQUIRE(motion::paceAt(motion::stopCount) == motion::Pace::Slow);
    REQUIRE(motion::paceAt(99) == motion::Pace::Slow);
}

// THE NUMBERS, in milliseconds: Normal is table.js's own 360 and 480, Quick half
// of that (section 18.2's budget is 180 for a card to the table), Slow the
// mock-up's "Relaxed" 1.6 times, and Off nothing at all.
TEST_CASE("Each pace says how long a flight and a sweep take", "[motion]")
{
    using motion::Pace;

    REQUIRE(motion::flightDuration(Pace::Off) == 0);
    REQUIRE(motion::sweepDuration(Pace::Off) == 0);

    REQUIRE(motion::flightDuration(Pace::Quick) == 180);
    REQUIRE(motion::sweepDuration(Pace::Quick) == 240);

    REQUIRE(motion::flightDuration(Pace::Normal) == 360);
    REQUIRE(motion::sweepDuration(Pace::Normal) == 480);

    REQUIRE(motion::flightDuration(Pace::Slow) == 576);
    REQUIRE(motion::sweepDuration(Pace::Slow) == 768);

    // Longer to the right, so the slider reads as "more motion".
    for(int stop = 1 ; stop < motion::stopCount ; stop++)
    {
        REQUIRE(motion::flightDuration(motion::paceAt(stop)) > motion::flightDuration(motion::paceAt(stop - 1)));
        REQUIRE(motion::sweepDuration(motion::paceAt(stop)) > motion::sweepDuration(motion::paceAt(stop - 1)));
    }
}

TEST_CASE("Only Off does not animate", "[motion]")
{
    REQUIRE_FALSE(motion::animates(motion::Pace::Off));

    for(const motion::Pace pace : { motion::Pace::Quick, motion::Pace::Normal, motion::Pace::Slow })
        REQUIRE(motion::animates(pace));
}

// page.html's bubble runs 1.6 s whatever the pace: reading time. At Off it is held
// still for 1.5 s, the mock-up's own reduced-motion rule.
TEST_CASE("The bubble's time does not follow the pace", "[motion]")
{
    REQUIRE(motion::bubbleMs == 1600);
    REQUIRE(motion::bubbleStillMs == 1500);
    REQUIRE(motion::bubbleStillMs < motion::bubbleMs);
}

// cubic-bezier(.2, .75, .25, 1): quick off the mark and then settling, as a card
// laid on a table does. Checked at the ends and at a quarter of the way, where it
// has covered most of its distance.
TEST_CASE("A card's flight eases out", "[motion]")
{
    const QEasingCurve curve = motion::flightCurve();

    REQUIRE(curve.valueForProgress(0.0) == Approx(0.0).margin(1e-6));
    REQUIRE(curve.valueForProgress(1.0) == Approx(1.0).margin(1e-6));

    // Worked out from the bezier: x = 0.25 is at s = 0.445, where y = 0.73.
    REQUIRE(curve.valueForProgress(0.25) == Approx(0.73).margin(0.03));
    REQUIRE(curve.valueForProgress(0.25) > 0.6);
}

// cubic-bezier(.5, 0, .75, 0): barely moving at first and then gathering speed, as
// a trick being drawn off the table does.
TEST_CASE("The sweep eases in", "[motion]")
{
    const QEasingCurve curve = motion::sweepCurve();

    REQUIRE(curve.valueForProgress(0.0) == Approx(0.0).margin(1e-6));
    REQUIRE(curve.valueForProgress(1.0) == Approx(1.0).margin(1e-6));

    // x = 0.5 is at s = 0.4, where y = 0.064: half the time, a sixteenth of the way.
    REQUIRE(curve.valueForProgress(0.5) == Approx(0.064).margin(0.03));
    REQUIRE(curve.valueForProgress(0.5) < 0.15);
}

TEST_CASE("Neither curve ever runs backwards", "[motion]")
{
    for(const QEasingCurve& curve : { motion::flightCurve(), motion::sweepCurve() })
    {
        qreal last = 0.0;

        for(int step = 1 ; step <= 100 ; step++)
        {
            const qreal value = curve.valueForProgress(step / 100.0);

            REQUIRE(value >= last - 1e-9);
            last = value;
        }
    }
}

TEST_CASE("A scene on its own starts with its motion off", "[motion]")
{
    TableScene scene;

    REQUIRE(scene.motionPace() == motion::Pace::Off);

    scene.setMotion(motion::Pace::Slow);
    REQUIRE(scene.motionPace() == motion::Pace::Slow);

    scene.setMotion(motion::Pace::Off);
    REQUIRE(scene.motionPace() == motion::Pace::Off);
}
