#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "paint/Theme.h"

#include <QImage>
#include <QPixmap>
#include <QPixmapCache>
#include <QSize>

#include <cmath>
#include <cstddef>
#include <iterator>

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

    REQUIRE(theme.background(QSize(320, 200), 1.0).size() == QSize(320, 200));
    REQUIRE(theme.avatar(0, QSize(48, 48), 1.0).size() == QSize(48, 48));

    // An empty request is an empty pixmap rather than a QPixmap of undefined
    // size, which is what a widget asks for on its first layout pass.
    REQUIRE(theme.background(QSize(), 1.0).isNull());
    REQUIRE(theme.avatar(0, QSize(), 1.0).isNull());
}

// THE PROPERTY THAT MATTERS WHEN THE ART ARRIVES. Theme is a copyable value that
// widgets hold by value; a per-copy cache means every copy repainting - or, once
// background() is loading and rescaling a photograph rather than filling a
// gradient, every copy re-loading it.
TEST_CASE("Copies of a theme share one background cache", "[theme]")
{
    const Theme theme = Theme::light();
    const QSize view(320, 200);

    const QPixmap first = theme.background(view, 1.0);

    // Identity, not equality: the same pixels, so no second paint happened.
    REQUIRE(theme.background(view, 1.0).cacheKey() == first.cacheKey());

    const Theme copy = theme;
    REQUIRE(copy.background(view, 1.0).cacheKey() == first.cacheKey());

    // And a different size is genuinely repainted rather than served stale.
    REQUIRE(theme.background(QSize(640, 400), 1.0).cacheKey() != first.cacheKey());
}

// The avatars are cached for ONE box size, and a second size discards the first
// set rather than joining it. Every seat's plaque reserves the same box at any
// moment, so a new size means the window was resized and the old set is dead -
// and a map that kept both would grow for as long as a resize drag lasted.
TEST_CASE("Avatars are cached, and a new box size replaces the set", "[theme]")
{
    const Theme theme = Theme::light();

    const QPixmap first = theme.avatar(0, QSize(48, 48), 1.0);

    // Same seat, same box: the stored pixels, not a second paint.
    REQUIRE(theme.avatar(0, QSize(48, 48), 1.0).cacheKey() == first.cacheKey());

    // A different seat at the same box is a different avatar, not the same one
    // handed back - the seat has to be in the key at all.
    REQUIRE(theme.avatar(1, QSize(48, 48), 1.0).cacheKey() != first.cacheKey());

    // A different box repaints, and then the original size repaints again
    // because the set it was in was discarded.
    REQUIRE(theme.avatar(0, QSize(64, 64), 1.0).size() == QSize(64, 64));
    REQUIRE(theme.avatar(0, QSize(48, 48), 1.0).cacheKey() != first.cacheKey());
}

// The palette members are public, so a caller may assign one. Keying the cache
// on the size alone would then keep serving a background painted in the colour
// that was replaced, which reads as the assignment silently not having taken.
TEST_CASE("Recolouring a theme invalidates its cached background", "[theme]")
{
    Theme theme = Theme::light();
    const QSize view(320, 200);

    const QPixmap before = theme.background(view, 1.0);

    theme.tableSurface = Theme::dark().tableSurface;

    REQUIRE(theme.background(view, 1.0).cacheKey() != before.cacheKey());
}

// The same rule for the avatar, and it is the one that was actually wrong: the
// avatar cache was keyed on seatPlaque, but paintAvatar() derives its per-seat
// tint from ACCENT as well, so this assignment used to hand back the identical
// pixmap. WHICH COLOURS A PAINTER READS IS NOT VISIBLE FROM THE LOOKUP, which is
// why the key is the whole palette rather than the members named at the top of
// the painter beneath it.
TEST_CASE("Recolouring the accent invalidates the cached avatars", "[theme]")
{
    Theme theme = Theme::light();
    const QSize box(48, 48);

    const QPixmap before = theme.avatar(0, box, 1.0);

    theme.accent = Theme::dark().accent;

    const QPixmap after = theme.avatar(0, box, 1.0);

    REQUIRE(after.cacheKey() != before.cacheKey());

    // Repainted AND different: a cache miss that produced the same picture would
    // pass the line above and still mean the accent had not been read.
    REQUIRE(after.toImage() != before.toImage());
}

// THE KEY LEADS ITS PAINTERS, which is the half of the rule above that only a
// colour NOTHING READS YET can assert. B2 added the three bid/won colours to the
// struct and left paletteKey() hashing the five that came before them - the
// identical omission the accent case above records, caught this time before a
// painter existed to make it visible.
//
// Deliberately no toImage() companion assertion, unlike that case: nothing
// paints a bid colour today, so the two pictures ARE the same and the pixels
// prove nothing. The cacheKey is the whole assertion - it says the cache would
// already have noticed, so section 14's pill can start reading bidOwing without
// this file or Theme.cpp needing a matching edit.
TEST_CASE("Recolouring a bid colour invalidates the cached background", "[theme]")
{
    Theme theme = Theme::light();
    const Theme other = Theme::dark();
    const QSize view(320, 200);

    // One at a time, because they were added together and a key maintained by
    // hand is a key that ends up with two of the three.
    const QPixmap before = theme.background(view, 1.0);

    theme.bidOwing = other.bidOwing;
    const QPixmap afterOwing = theme.background(view, 1.0);
    REQUIRE(afterOwing.cacheKey() != before.cacheKey());

    theme.bidOnTarget = other.bidOnTarget;
    const QPixmap afterOnTarget = theme.background(view, 1.0);
    REQUIRE(afterOnTarget.cacheKey() != afterOwing.cacheKey());

    theme.bidOver = other.bidOver;
    REQUIRE(theme.background(view, 1.0).cacheKey() != afterOnTarget.cacheKey());
}

// THE SAME CONTRACT CardRenderer HAS, and it has to be the same one: the table
// and the cards are drawn beside each other in one frame, so a Theme that
// rasterizes at the logical size is a soft background behind crisp cards, which
// is the half of "cards soften on a HiDPI display" that the card cache's own dpr
// key does not cover.
TEST_CASE("The background and the avatar are rasterized at the device pixel ratio", "[theme]")
{
    const Theme theme = Theme::light();
    const QSize view(320, 200);
    const QSize box(48, 48);

    const QPixmap background = theme.background(view, 2.0);

    REQUIRE(background.size() == QSize(640, 400));
    REQUIRE(background.devicePixelRatio() == Catch::Approx(2.0));

    // And still laid out at the size that was asked for, which is the point of
    // stamping the ratio on rather than handing the caller a bigger pixmap.
    REQUIRE(background.deviceIndependentSize().toSize() == view);

    const QPixmap avatar = theme.avatar(0, box, 2.0);

    REQUIRE(avatar.size() == QSize(96, 96));
    REQUIRE(avatar.devicePixelRatio() == Catch::Approx(2.0));
    REQUIRE(avatar.deviceIndependentSize().toSize() == box);
}

// Rasterizing at the ratio is only half of it: without the ratio in the KEY, a
// window dragged from a retina screen to a plain one - or the reverse - keeps
// the pixmap painted for the screen it left.
TEST_CASE("The theme's caches are keyed on the device pixel ratio", "[theme]")
{
    const Theme theme = Theme::light();
    const QSize view(320, 200);
    const QSize box(48, 48);

    REQUIRE(theme.background(view, 1.0).cacheKey() != theme.background(view, 2.0).cacheKey());
    REQUIRE(theme.avatar(0, box, 1.0).cacheKey() != theme.avatar(0, box, 2.0).cacheKey());

    // A ratio of zero or less is not a screen; it is a caller with an
    // uninitialised value, and it must not divide the layout by it or paint a
    // one-pixel table. Same guard CardRenderer::face() has.
    REQUIRE(theme.background(view, 0.0).isNull());
    REQUIRE(theme.avatar(0, box, -1.0).isNull());
}

// The same rule the bid colours needed in B2a, for the two the scoreboard added
// in B2b. Nothing in Theme's own painters reads either one - they are drawn by
// the scoreboard, which has a palette of its own to consult - so this is the
// rule holding for colours no painter here touches, which is exactly the case
// it exists for.
TEST_CASE("Recolouring a score colour invalidates the cached background", "[theme]")
{
    const QSize view(320, 200);

    {
        Theme theme = Theme::light();
        const QPixmap before = theme.background(view, 1.0);

        theme.scorePositive = Theme::dark().scorePositive;

        REQUIRE(theme.background(view, 1.0).cacheKey() != before.cacheKey());
    }

    {
        Theme theme = Theme::light();
        const QPixmap before = theme.background(view, 1.0);

        theme.scoreNegative = Theme::dark().scoreNegative;

        REQUIRE(theme.background(view, 1.0).cacheKey() != before.cacheKey());
    }
}

// Section 18 asks for green and red by name, and dark mode must keep them the
// same two colours a player has already learned rather than recolouring them.
TEST_CASE("The score colours are a green and a red in both palettes", "[theme]")
{
    for(const Theme& theme : { Theme::light(), Theme::dark() })
    {
        REQUIRE(theme.scorePositive.green() > theme.scorePositive.red());
        REQUIRE(theme.scoreNegative.red() > theme.scoreNegative.green());
    }
}

// A NUMBER, because "does that read?" was answered by eye once and answered
// wrong. C4 painted section 17's round-type label in `accent`, and against the
// plaque the light theme's amber measures 2.0:1 - under the 3:1 floor even for
// large text, and far under the 14.97:1 the three labels beside it kept. The
// word was still legible-ish and the mistake still shipped, which is the whole
// argument for asserting the ratio rather than trusting a look at a screenshot.
//
// WCAG 2.x relative luminance, spelled out here rather than pulled in: it is
// eight lines, Qt has no equivalent, and a contrast rule nothing can evaluate is
// a comment rather than a constraint.
static double relativeLuminance(const QColor& colour)
{
    auto channel = [](double srgb) {
        return srgb <= 0.03928 ? srgb / 12.92
                               : std::pow((srgb + 0.055) / 1.055, 2.4);
    };

    return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF())
           + 0.0722 * channel(colour.blueF());
}

static double contrastRatio(const QColor& text, const QColor& background)
{
    const double one = relativeLuminance(text);
    const double two = relativeLuminance(background);

    return (std::max(one, two) + 0.05) / (std::min(one, two) + 0.05);
}

TEST_CASE("Every colour drawn as text on the plaque holds AA against it", "[theme]")
{
    for(const Theme& theme : { Theme::light(), Theme::dark() })
    {
        // The regression this case exists for. accentText is the round type in
        // InfoStrip and nothing else yet; if a later phase reaches for `accent`
        // to colour another word, this is the line that should stop it.
        REQUIRE(contrastRatio(theme.accentText, theme.seatPlaque) >= 4.5);

        // Its neighbours, in the same case rather than a second one: they are
        // all text on the same plaque, and the point is the RULE, not the one
        // colour that broke it. The bid and score colours read on the plaque
        // too - the pill and the scoreboard both fill with seatPlaque.
        REQUIRE(contrastRatio(theme.textPrimary, theme.seatPlaque) >= 4.5);
        REQUIRE(contrastRatio(theme.bidOnTarget, theme.seatPlaque) >= 4.5);
        REQUIRE(contrastRatio(theme.bidOwing, theme.seatPlaque) >= 4.5);
        REQUIRE(contrastRatio(theme.bidOver, theme.seatPlaque) >= 4.5);
        REQUIRE(contrastRatio(theme.scorePositive, theme.seatPlaque) >= 4.5);
        REQUIRE(contrastRatio(theme.scoreNegative, theme.seatPlaque) >= 4.5);
    }
}

// The other half of the split, and the half a contrast number cannot say: the
// two ambers have to stay ONE colour to a player. A darkened accent is a shade
// of the same thing; an unrelated brown would be a second colour to learn, and
// would pass the ratio above just as happily.
TEST_CASE("The text accent is the accent, darkened", "[theme]")
{
    for(const Theme& theme : { Theme::light(), Theme::dark() })
    {
        REQUIRE(std::abs(theme.accentText.hslHue() - theme.accent.hslHue()) <= 10);
        REQUIRE(theme.accentText.lightnessF() <= theme.accent.lightnessF());
    }
}

// THE KEY LEADS ITS PAINTERS again, for the colour C4a added. Nothing Theme
// paints reads accentText - InfoStrip colours a QLabel with it - so this is the
// bid-colour case's argument one more time, and the reason it is written now
// rather than when a painter finally does read it.
TEST_CASE("Recolouring the text accent invalidates the cached background", "[theme]")
{
    Theme theme = Theme::light();
    const QSize view(320, 200);

    const QPixmap before = theme.background(view, 1.0);

    theme.accentText = Theme::dark().accentText;

    REQUIRE(theme.background(view, 1.0).cacheKey() != before.cacheKey());
}

// ---------------------------------------------------------------------------
// The table redesign (docs/ui-redesign/), phase 1: the textures and the palette
// arrive before anything draws them, so these assert what can be asserted of an
// input - that it is there, that it is shared, and that the key sees it.
// ---------------------------------------------------------------------------

// RESOURCE EMBEDDING FAILS SILENTLY, and CMakeLists.txt lists the ways. A size is
// the cheapest assertion that the right file was compiled into the right binary
// and decoded - a null pixmap is 0x0, and the two textures differ in size from
// each other, so a swapped alias fails too.
TEST_CASE("The table textures load from the resources at their native sizes", "[theme]")
{
    const Theme theme = Theme::dark();

    const QPixmap wood = theme.woodTexture();
    REQUIRE(wood.size() == QSize(1020, 1020));

    // THE RIM IS A RING. Its middle is transparent so the felt can show through,
    // and its corners are outside the table; a load that dropped the alpha would
    // paint a brown square over everything. The rim itself is opaque.
    REQUIRE(wood.hasAlphaChannel());
    const QImage woodImage = wood.toImage();
    REQUIRE(woodImage.pixelColor(510, 510).alpha() == 0);
    REQUIRE(woodImage.pixelColor(0, 0).alpha() == 0);
    REQUIRE(woodImage.pixelColor(510, 40).alpha() == 255);

    REQUIRE(theme.feltTexture().size() == QSize(880, 880));

    // Source art, not a raster for any one screen: scaling to the view and its
    // ratio is the composed table's job, not this accessor's.
    REQUIRE(wood.devicePixelRatio() == Catch::Approx(1.0));

    // A default-constructed Theme has no cache, and still gets its textures -
    // the same fallback background() makes.
    const Theme bare;
    REQUIRE(bare.woodTexture().size() == QSize(1020, 1020));
    REQUIRE(bare.feltTexture().size() == QSize(880, 880));
}

// The background cache's property, for the textures: Theme is held by value in
// a dozen widgets, and each copy decoding its own megabyte of PNG is the cost
// the shared cache exists to avoid.
TEST_CASE("Copies of a theme share one load of each texture", "[theme]")
{
    const Theme theme = Theme::light();

    const QPixmap wood = theme.woodTexture();
    const QPixmap felt = theme.feltTexture();

    // QPIXMAP KEEPS A CACHE OF ITS OWN. Loading a file consults the process-wide
    // QPixmapCache first, which would hand a second load the same pixels and make
    // every identity below hold with Theme caching nothing at all. Emptying it
    // leaves Theme's cache as the only thing that can.
    QPixmapCache::clear();

    // The control that proves the line above did its job: a theme with no cache
    // of its own now really does load afresh, and gets different pixels.
    REQUIRE(Theme().woodTexture().cacheKey() != wood.cacheKey());

    REQUIRE(theme.woodTexture().cacheKey() == wood.cacheKey());

    const Theme copy = theme;
    REQUIRE(copy.woodTexture().cacheKey() == wood.cacheKey());
    REQUIRE(copy.feltTexture().cacheKey() == felt.cacheKey());

    // And a recolour does not reload: a texture is not a colour, so the palette
    // is no part of its key. The base colour UNDER it changes; the grain does not.
    Theme recoloured = theme;
    recoloured.table.woodBase = Theme::light().table.feltBase;
    REQUIRE(recoloured.woodTexture().cacheKey() == wood.cacheKey());
}

// Q1, decided by the owner on 2026-10-17: the redesign is dark only, so the
// table page does not follow the system scheme. The case that matters is the
// first one - a player whose desktop is light still gets the dark table.
TEST_CASE("The table page is the same dark palette in every scheme", "[theme]")
{
    REQUIRE(Theme::light().table == Theme::dark().table);
    REQUIRE(Theme::forScheme(Qt::ColorScheme::Unknown).table == Theme::dark().table);

    // Not merely the same, but dark: light text on darker surfaces. Two copies
    // of a LIGHT palette would pass the equality above.
    const Theme::TablePalette table = Theme::light().table;
    REQUIRE(table.cream.lightness() > table.panelTop.lightness());
    REQUIRE(table.cream.lightness() > table.roomCentre.lightness());
    REQUIRE(table.cream.lightness() > table.feltBase.lightness());
}

// THE KEY LEADS ITS PAINTERS, for twenty-three colours at once. One case per
// colour, as the earlier ones above were written, would be twenty-three cases
// maintained by hand, which is exactly the habit that left three bid colours
// out of the key in B2. So this one is a table, and the table is CHECKED: its
// length must equal the number of members the struct actually has.
//
// That count comes from sizeof, and is only right while TablePalette holds
// nothing but QColors - which Theme.h states beside the struct for this reason.
TEST_CASE("Recolouring any table colour invalidates the cached background", "[theme]")
{
    using Palette = Theme::TablePalette;

    struct Member
    {
        QColor Palette::* colour;
        const char* name;
    };

    const Member members[] = {
        { &Palette::roomCentre, "roomCentre" },
        { &Palette::roomMiddle, "roomMiddle" },
        { &Palette::roomEdge, "roomEdge" },
        { &Palette::woodBase, "woodBase" },
        { &Palette::feltBase, "feltBase" },
        { &Palette::panelTop, "panelTop" },
        { &Palette::panelBottom, "panelBottom" },
        { &Palette::panelBorder, "panelBorder" },
        { &Palette::panelInnerShadow, "panelInnerShadow" },
        { &Palette::panelInnerLine, "panelInnerLine" },
        { &Palette::panelShadow, "panelShadow" },
        { &Palette::gold, "gold" },
        { &Palette::goldBright, "goldBright" },
        { &Palette::cream, "cream" },
        { &Palette::muted, "muted" },
        { &Palette::good, "good" },
        { &Palette::warn, "warn" },
        { &Palette::bad, "bad" },
        { &Palette::redSuit, "redSuit" },
        { &Palette::blackSuit, "blackSuit" },
        { &Palette::buttonTop, "buttonTop" },
        { &Palette::buttonBottom, "buttonBottom" },
        { &Palette::buttonBorder, "buttonBorder" },
    };

    // A member added to the struct and not to this table fails here, before it
    // can fail to be tested below.
    REQUIRE(std::size(members) == sizeof(Palette) / sizeof(QColor));

    // And a duplicate would let one member stand in for another while the count
    // still matched.
    for(std::size_t first = 0; first < std::size(members); ++first)
    {
        for(std::size_t second = first + 1; second < std::size(members); ++second)
            REQUIRE(members[first].colour != members[second].colour);
    }

    // A colour none of them holds, so every assignment below is a change.
    const QColor recolour(1, 2, 3);
    const QSize view(320, 200);

    for(const Member& member : members)
    {
        INFO("TablePalette::" << member.name);

        Theme theme = Theme::light();
        REQUIRE(theme.table.*member.colour != recolour);

        const QPixmap before = theme.background(view, 1.0);

        theme.table.*member.colour = recolour;

        REQUIRE(theme.background(view, 1.0).cacheKey() != before.cacheKey());
    }
}

// The plaque case above, for the redesign's panel - the surface every word on
// the new table page is written on. Both ends of its gradient, with the alpha
// dropped: at 0.95 over a near-black room, the opaque colour is the LIGHTER of
// the two possibilities and so the worst case for light text.
TEST_CASE("Every table-page text colour holds AA against the panel", "[theme]")
{
    const Theme::TablePalette table = Theme::dark().table;

    for(const QColor& panel : { table.panelTop, table.panelBottom })
    {
        for(const QColor& text : { table.cream, table.muted, table.gold, table.goldBright,
                                   table.good, table.warn, table.bad })
        {
            INFO(text.name().toStdString() << " on " << panel.name().toStdString());
            REQUIRE(contrastRatio(text, panel) >= 4.5);
        }

        // THE RED SUIT IS A MARK, NOT A WORD - accentText's argument, the other
        // way round. A ♥ or ♦ is recognised by its shape, which says the suit on
        // its own, so it is held to the 3:1 non-text floor rather than to AA.
        // It measures about 4.2:1 on the lighter end; if a later phase starts
        // writing words in it, this is the line that needs to become 4.5.
        REQUIRE(contrastRatio(table.redSuit, panel) >= 3.0);
    }
}
