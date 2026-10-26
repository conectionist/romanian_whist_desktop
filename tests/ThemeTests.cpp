#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "paint/Avatars.h"
#include "paint/Theme.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QSize>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// Theme is mostly a palette, and a palette has no test worth writing - a colour
// is right or wrong by eye. What IS testable is the part with a rule: the
// properties that make swapping the art later cheap rather than a rewrite, and
// the caches that keep a repaint from costing a paint.
//
// The card height every table metric used to derive from was tested here too,
// until the table redesign's phase 2 replaced it with fixed table space
// (TableSpaceTests.cpp).

namespace
{
// The catalogue's first two avatars (resources/images/avatars/avatars.json).
const std::string firstAvatar = "man-dark-hair-beard";
const std::string secondAvatar = "man-hat-mustache";
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
    REQUIRE(theme.avatar(firstAvatar, QSize(48, 48), 1.0).size() == QSize(48, 48));

    // An empty request is an empty pixmap rather than a QPixmap of undefined
    // size, which is what a widget asks for on its first layout pass.
    REQUIRE(theme.background(QSize(), 1.0).isNull());
    REQUIRE(theme.avatar(firstAvatar, QSize(), 1.0).isNull());
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

    const QPixmap first = theme.avatar(firstAvatar, QSize(48, 48), 1.0);

    // Same avatar, same box: the stored pixels, not a second paint.
    REQUIRE(theme.avatar(firstAvatar, QSize(48, 48), 1.0).cacheKey() == first.cacheKey());

    // A different avatar at the same box is a different picture, not the same one
    // handed back - the id has to be in the key at all.
    REQUIRE(theme.avatar(secondAvatar, QSize(48, 48), 1.0).cacheKey() != first.cacheKey());

    // A different box repaints, and then the original size repaints again
    // because the set it was in was discarded.
    REQUIRE(theme.avatar(firstAvatar, QSize(64, 64), 1.0).size() == QSize(64, 64));
    REQUIRE(theme.avatar(firstAvatar, QSize(48, 48), 1.0).cacheKey() != first.cacheKey());
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

// EVERY AVATAR IS ITS OWN PERSON: no two files in the catalogue draw the same
// picture. Since the owner's call of 2026-10-23 the portraits are files
// (avatars::catalogue()), and the six the table mock-up drew in code are the
// first six.
TEST_CASE("Every avatar in the catalogue is a different picture", "[theme]")
{
    const Theme theme = Theme::dark();
    const QSize box(86, 98);

    REQUIRE(avatars::catalogue().size() >= 6);

    std::vector<QImage> seen;

    for(const avatars::Avatar& avatar : avatars::catalogue())
    {
        INFO("avatar " << avatar.id);

        const QImage image = theme.avatar(avatar.id, box, 1.0).toImage();

        REQUIRE(image.size() == box);

        for(const QImage& other : seen)
            REQUIRE(image != other);

        seen.push_back(image);
    }
}

// A MISSING FACE SHOWS AND HARMS NOTHING: an id the catalogue does not have, or
// none, draws the box's bare dark, through the same filter as every portrait.
TEST_CASE("An avatar the catalogue does not have draws the bare box", "[theme]")
{
    const Theme theme = Theme::dark();
    const QSize box(86, 98);

    for(const std::string& id : { std::string("no-such-avatar"), std::string() })
    {
        const QImage image = theme.avatar(id, box, 1.0).toImage();

        REQUIRE(image.size() == box);

        const QRgb corner = image.pixel(0, 0);

        for(const QPoint point : { QPoint(43, 49), QPoint(85, 97), QPoint(10, 80) })
            REQUIRE(image.pixel(point) == corner);

        // And it is not a real avatar's picture.
        REQUIRE(image != theme.avatar(firstAvatar, box, 1.0).toImage());
    }
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

    const QPixmap avatar = theme.avatar(firstAvatar, box, 2.0);

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
    REQUIRE(theme.avatar(firstAvatar, box, 1.0).cacheKey() != theme.avatar(firstAvatar, box, 2.0).cacheKey());

    // A ratio of zero or less is not a screen; it is a caller with an
    // uninitialised value, and it must not divide the layout by it or paint a
    // one-pixel table. Same guard CardRenderer::face() has.
    REQUIRE(theme.background(view, 0.0).isNull());
    REQUIRE(theme.avatar(firstAvatar, box, -1.0).isNull());
}

// Section 18 asks for green and red by name - a gain and a loss on the round
// sheet, the pill on target and over - and since the table redesign's phase 10
// moved the last of them across, those are the table's own good and bad, the
// same in every scheme.
TEST_CASE("The table's good and bad are a green and a red", "[theme]")
{
    for(const Theme& theme : { Theme::light(), Theme::dark() })
    {
        REQUIRE(theme.table.good.green() > theme.table.good.red());
        REQUIRE(theme.table.bad.red() > theme.table.bad.green());
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

    // THE CONTROL: a theme with no cache of its own loads afresh and gets
    // different pixels. Without it, every identity below could hold with Theme
    // caching nothing at all. It is also the line that fails if the load goes
    // back to QPixmap(path), which files every file it reads in the process-wide
    // QPixmapCache and would hand this load the same pixels (Theme.cpp has why
    // that cache is avoided).
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

// THE CACHES COMPARE EVERY COLOUR BY CONSTRUCTION: what they store is a copy of
// ThemeColours, compared with its defaulted operator==, so no list of members
// exists anywhere for a new colour to be left off. One table colour stands for
// all forty-four here - a case per member would test the compiler.
//
// The LAST member, because a hand-written comparison that stopped short would
// miss it first; and every cache, because each keeps its own copy - the room's
// since the table redesign's phase 2.
TEST_CASE("Recolouring a table colour invalidates every cache", "[theme]")
{
    Theme theme = Theme::light();
    const QSize view(320, 200);
    const QSize box(48, 48);

    const QPixmap background = theme.background(view, 1.0);
    const QPixmap room = theme.room(view, 1.0);
    const QPixmap avatar = theme.avatar(firstAvatar, box, 1.0);

    // A colour the palette does not hold, so the assignment is a change.
    const QColor recolour(1, 2, 3);
    REQUIRE(theme.table.logoOrnament != recolour);
    theme.table.logoOrnament = recolour;

    REQUIRE(theme.background(view, 1.0).cacheKey() != background.cacheKey());
    REQUIRE(theme.room(view, 1.0).cacheKey() != room.cacheKey());
    REQUIRE(theme.avatar(firstAvatar, box, 1.0).cacheKey() != avatar.cacheKey());
}

// ---- the room behind the table, added in the table redesign's phase 2 ----

// background()'s contract, held for the room: the box asked for, rasterized at
// the ratio and laid out at the box, one paint shared by every copy, and a new
// size or ratio painted again rather than served stale.
TEST_CASE("The room is rasterized at the ratio and cached like the background", "[theme]")
{
    const Theme theme = Theme::light();
    const QSize view(320, 200);

    const QPixmap room = theme.room(view, 2.0);

    REQUIRE(room.size() == QSize(640, 400));
    REQUIRE(room.devicePixelRatio() == Catch::Approx(2.0));
    REQUIRE(room.deviceIndependentSize().toSize() == view);

    const Theme copy = theme;
    REQUIRE(copy.room(view, 2.0).cacheKey() == room.cacheKey());

    REQUIRE(theme.room(view, 1.0).cacheKey() != room.cacheKey());
    REQUIRE(theme.room(QSize(640, 400), 2.0).cacheKey() != room.cacheKey());

    REQUIRE(theme.room(QSize(), 1.0).isNull());
    REQUIRE(theme.room(view, 0.0).isNull());
}

namespace
{
// Within a step or two of eight-bit rounding: a gradient is interpolated, and the
// pixel at a stop is the stop's colour only to within that.
bool roughly(const QColor& got, const QColor& want)
{
    constexpr int slack = 3;

    return std::abs(got.red() - want.red()) <= slack
        && std::abs(got.green() - want.green()) <= slack
        && std::abs(got.blue() - want.blue()) <= slack;
}
}

// page.html's `radial-gradient(ellipse at 50% 42%, ...)`, whose size is CSS's
// default, farthest-corner. That puts the centre colour at (50%, 42%) and the
// edge colour exactly at the two BOTTOM corners, the farthest from a centre above
// the middle - and NOT at the top two, which are nearer and so still lighter.
// A circle, or an ellipse sized to the box's half-width and half-height, puts the
// edge colour somewhere else, which is what the last two assertions would see.
TEST_CASE("The room is section 2.2's gradient", "[theme]")
{
    const Theme theme = Theme::dark();
    const QSize view(400, 300);

    const QImage room = theme.room(view, 1.0).toImage();

    REQUIRE(roughly(room.pixelColor(200, 126), theme.table.roomCentre));
    REQUIRE(roughly(room.pixelColor(0, 299), theme.table.roomEdge));
    REQUIRE(roughly(room.pixelColor(399, 299), theme.table.roomEdge));

    REQUIRE_FALSE(roughly(room.pixelColor(0, 0), theme.table.roomEdge));
    REQUIRE(room.pixelColor(0, 0).lightness() > theme.table.roomEdge.lightness());
}

// What a translucent colour looks like once it is painted over an opaque one:
// QPainter's source-over, channel by channel in sRGB, as it blends by default.
static QColor over(const QColor& top, const QColor& backdrop)
{
    const float alpha = top.alphaF();
    auto blend = [alpha](float source, float destination) {
        return alpha * source + (1.0f - alpha) * destination;
    };

    return QColor::fromRgbF(blend(top.redF(), backdrop.redF()),
                            blend(top.greenF(), backdrop.greenF()),
                            blend(top.blueF(), backdrop.blueF()));
}

// The plaque case above, for the redesign's panel - the surface every word on
// the new table page is written on. Both ends of its gradient.
//
// COMPOSITED OVER WHITE, because the panel is translucent and its backdrop is
// the worst case, not the panel's own colour. At 0.95 a panel lets a little of
// whatever is under it through - the room's centre, the wood, the felt, a card -
// and every one of those is lighter than panelTop, so the panel as drawn is
// lighter than the panel as declared. White is the lightest backdrop there is,
// which makes this a bound for wherever a later phase puts a panel. It costs
// about a tenth of each ratio, and every colour here clears its floor anyway.
TEST_CASE("Every table-page text colour holds AA against the panel", "[theme]")
{
    const Theme::TablePalette table = Theme::dark().table;

    for(const QColor& translucent : { table.panelTop, table.panelBottom })
    {
        const QColor panel = over(translucent, Qt::white);

        for(const QColor& text : { table.cream, table.muted, table.gold, table.goldBright,
                                   table.good, table.warn, table.bad })
        {
            INFO(text.name().toStdString() << " on " << panel.name().toStdString());
            REQUIRE(contrastRatio(text, panel) >= 4.5);
        }

        // THE RED SUIT IS A MARK, NOT A WORD - the argument the old palette's
        // text accent was split out on, the other way round. A ♥ or ♦ is
        // recognised by its shape, which says the suit on
        // its own, so it is held to the 3:1 non-text floor rather than to AA.
        // It measures about 3.7:1 on the lighter end; if a later phase starts
        // writing words in it, this is the line that needs to become 4.5.
        REQUIRE(contrastRatio(table.redSuit, panel) >= 3.0);
    }
}

// page.html's .portrait filter is on the WHOLE ELEMENT, its gold border included,
// so the frame is #a3824a through sepia(0.18) contrast(1.05): about rgb(168, 136,
// 81), worked through by hand. Painted raw, every frame was cooler and darker than
// the mock-up's.
TEST_CASE("A portrait's frame is filtered as its picture is", "[theme]")
{
    for(const Theme& theme : { Theme::light(), Theme::dark() })
    {
        const QColor frame = theme.table.portraitBorder;

        REQUIRE(std::abs(frame.red() - 168) <= 1);
        REQUIRE(std::abs(frame.green() - 136) <= 1);
        REQUIRE(std::abs(frame.blue() - 81) <= 1);
    }
}
