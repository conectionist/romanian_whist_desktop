#ifndef MOTION_H
#define MOTION_H

#include <QEasingCurve>
#include <QtGlobal>

// HOW MUCH THE TABLE MOVES (the table redesign's phase 12): the one setting that
// decides it, and the numbers the animations read.
//
// A SETTING, NOT A CONSTANT, by the owner's call of 2026-10-22: the card's flight
// and the trick's sweep are the mock-up's own lengths (360 and 480 ms) by default,
// and a player who wants them quicker, slower or gone says so in Settings. Off is
// also the "reduce motion" of section 18.2: Qt 6.11 has no system setting to read
// (QAccessibilityHints only reports contrast), so the preference is ours.
//
// NO ITEM IN THIS FILE, only numbers and curves, so a test can say what each pace
// means without a scene.
namespace motion
{

// Four stops on Settings' slider, left to right, and the enumerators' values are
// the slider's positions.
enum class Pace
{
    Off,
    Quick,
    Normal,
    Slow,
};

// The mock-up's own, which is what the owner played and approved.
inline constexpr Pace defaultPace = Pace::Normal;

inline constexpr int stopCount = 4;

constexpr int stopOf(Pace pace)
{
    return static_cast<int>(pace);
}

// The pace at a slider position, clamped: a value out of range is the nearest stop
// rather than an enumerator that names nothing.
Pace paceAt(int stop);

// What a card's flight onto the felt and the trick's sweep to its winner take at
// Normal, from table.js's animateIn() (360 ms) and sweep() (480 ms).
inline constexpr int flightMs = 360;
inline constexpr int sweepMs = 480;

// THE BID BUBBLE'S 1.6 S (page.html's `animation: bubble 1.6s`) does not scale with
// the pace: it is reading time, not motion. Off shows it still for 1.5 s, which is
// the mock-up's own reduced-motion rule (`animation-delay: 1.5s`).
inline constexpr int bubbleMs = 1600;
inline constexpr int bubbleStillMs = 1500;

// Normal's length times this: 0, 0.5, 1 and 1.6 (the mock-up's own "Relaxed" pace).
// At Quick the lengths are 180 and 240 ms, which is section 18.2's budget for a card
// to the table.
qreal factorFor(Pace pace);

// In milliseconds, and zero at Off: nothing flies and nothing sweeps.
int flightDuration(Pace pace);
int sweepDuration(Pace pace);

inline bool animates(Pace pace)
{
    return pace != Pace::Off;
}

// table.js's two curves, as the CSS spells them: a card landing eases out, cubic-bezier(.2, .75,
// .25, 1), and the trick clearing eases in, cubic-bezier(.5, 0, .75, 0).
QEasingCurve flightCurve();
QEasingCurve sweepCurve();

} // namespace motion

#endif
