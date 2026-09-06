#include <catch2/catch_test_macros.hpp>

#include "paint/Theme.h"

#include <QPixmap>
#include <QSize>

// Theme is mostly a palette, and a palette has no test worth writing - a colour
// is right or wrong by eye. What IS testable is the part with a rule: the one
// metric every other metric on the table derives from, and the two properties
// that make swapping the art later cheap rather than a rewrite.

TEST_CASE("The card height follows the window and stays inside its clamps", "[theme]")
{
    const Theme theme = Theme::light();

    // Bigger window, bigger card - the property the whole "one unit" rule rests
    // on. A metric that ignored the view would pass every other test here.
    REQUIRE(theme.cardHeightFor(QSize(1280, 800)) > theme.cardHeightFor(QSize(640, 400)));

    // The floor holds where the arithmetic would go under it. Without the clamp
    // this window produces a 22-pixel card, which is a rectangle with no
    // readable rank on it.
    REQUIRE(theme.cardHeightFor(QSize(160, 100)) >= 44.0);

    // And the ceiling holds where it would run away. A 4K window would otherwise
    // deal cards the size of a hand of paper.
    REQUIRE(theme.cardHeightFor(QSize(3840, 2160)) <= 220.0);

    // A degenerate view is the floor, not zero and not a division by it.
    REQUIRE(theme.cardHeightFor(QSize()) == 44.0);
}

// The width limit is the half that is easy to leave out, because it only shows
// on a window that is wide and short - where a card sized from the height alone
// leaves no room for section 14's eight-card fan.
TEST_CASE("The card height is limited by the window's width as well as its height", "[theme]")
{
    const Theme theme = Theme::light();

    // Same height, very different widths. Sizing from the height alone would
    // return the same card for both.
    REQUIRE(theme.cardHeightFor(QSize(400, 900)) < theme.cardHeightFor(QSize(1400, 900)));
}

TEST_CASE("Light and dark are different palettes", "[theme]")
{
    const Theme light = Theme::light();
    const Theme dark = Theme::dark();

    REQUIRE(light.tableSurface != dark.tableSurface);
    REQUIRE(light.textPrimary != dark.textPrimary);

    // Not merely different: the text inverts, which is what makes it a dark mode
    // rather than a second light one. A copied palette passes the two above only
    // if someone nudged a channel.
    REQUIRE(light.textPrimary.lightness() < dark.textPrimary.lightness());
    REQUIRE(dark.tableSurface.lightness() < light.tableSurface.lightness());
}

TEST_CASE("An unknown colour scheme resolves to the light palette", "[theme]")
{
    // Unknown is what a platform that does not report a scheme returns. It is a
    // normal state rather than a fault, and it must not produce a default-
    // constructed Theme with no colours in it.
    REQUIRE(Theme::forScheme(Qt::ColorScheme::Unknown).tableSurface == Theme::light().tableSurface);
    REQUIRE(Theme::forScheme(Qt::ColorScheme::Dark).tableSurface == Theme::dark().tableSurface);
}

TEST_CASE("The background and the avatar fill the box they are given", "[theme]")
{
    const Theme theme = Theme::light();

    REQUIRE(theme.background(QSize(320, 200)).size() == QSize(320, 200));
    REQUIRE(theme.avatar(0, QSize(48, 48)).size() == QSize(48, 48));

    // An empty request is an empty pixmap rather than a QPixmap of undefined
    // size, which is what a widget asks for on its first layout pass.
    REQUIRE(theme.background(QSize()).isNull());
    REQUIRE(theme.avatar(0, QSize()).isNull());
}

// THE PROPERTY THAT MATTERS WHEN THE ART ARRIVES. Theme is a copyable value that
// widgets hold by value; a per-copy cache means every copy repainting - or, once
// background() is loading and rescaling a photograph rather than filling a
// gradient, every copy re-loading it.
TEST_CASE("Copies of a theme share one background cache", "[theme]")
{
    const Theme theme = Theme::light();
    const QSize view(320, 200);

    const QPixmap first = theme.background(view);

    // Identity, not equality: the same pixels, so no second paint happened.
    REQUIRE(theme.background(view).cacheKey() == first.cacheKey());

    const Theme copy = theme;
    REQUIRE(copy.background(view).cacheKey() == first.cacheKey());

    // And a different size is genuinely repainted rather than served stale.
    REQUIRE(theme.background(QSize(640, 400)).cacheKey() != first.cacheKey());
}

// The avatars are cached for ONE box size, and a second size discards the first
// set rather than joining it. Every seat's plaque reserves the same box at any
// moment, so a new size means the window was resized and the old set is dead -
// and a map that kept both would grow for as long as a resize drag lasted.
TEST_CASE("Avatars are cached, and a new box size replaces the set", "[theme]")
{
    const Theme theme = Theme::light();

    const QPixmap first = theme.avatar(0, QSize(48, 48));

    // Same seat, same box: the stored pixels, not a second paint.
    REQUIRE(theme.avatar(0, QSize(48, 48)).cacheKey() == first.cacheKey());

    // A different seat at the same box is a different avatar, not the same one
    // handed back - the seat has to be in the key at all.
    REQUIRE(theme.avatar(1, QSize(48, 48)).cacheKey() != first.cacheKey());

    // A different box repaints, and then the original size repaints again
    // because the set it was in was discarded.
    REQUIRE(theme.avatar(0, QSize(64, 64)).size() == QSize(64, 64));
    REQUIRE(theme.avatar(0, QSize(48, 48)).cacheKey() != first.cacheKey());
}

// The palette members are public, so a caller may assign one. Keying the cache
// on the size alone would then keep serving a background painted in the colour
// that was replaced, which reads as the assignment silently not having taken.
TEST_CASE("Recolouring a theme invalidates its cached background", "[theme]")
{
    Theme theme = Theme::light();
    const QSize view(320, 200);

    const QPixmap before = theme.background(view);

    theme.tableSurface = Theme::dark().tableSurface;

    REQUIRE(theme.background(view).cacheKey() != before.cacheKey());
}
