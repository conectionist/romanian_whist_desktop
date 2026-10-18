#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CardStringMaker.h"

#include "paint/CardRenderer.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QSvgRenderer>

#include <cstdlib>
#include <ios>
#include <vector>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// What CardRenderer has to be right about, and none of it is visible by looking
// at a table: that all 52 filenames resolve, that no two of them resolve to the
// SAME art, and that the four components of the cache key are all actually in
// it. A transposed switch arm and a cache blind to the device pixel ratio are
// both silent, and both are found in B2 as "that card is wrong" or "the cards
// look soft" - a phase later, with a scene on top to blame first.
namespace
{

std::vector<Card> allCards()
{
    std::vector<Card> cards;

    for(int rank = static_cast<int>(Rank::Two) ; rank <= static_cast<int>(Rank::Ace) ; rank++)
        for(int suit = static_cast<int>(Suit::Hearts) ; suit <= static_cast<int>(Suit::Clubs) ; suit++)
            cards.push_back(Card(static_cast<Rank>(rank), static_cast<Suit>(suit)));

    return cards;
}

// The pixels themselves, in one canonical format so that two images of the same
// card always compare equal and two images of different cards never do.
// Format_ARGB32 has no scanline padding at these widths, so the buffer is the
// image and nothing about the platform leaks into the comparison.
//
// DIGESTED RATHER THAN COMPARED RAW, which is not an optimization. Catch2 prints
// the operands of a failed comparison, and handing it tens of kilobytes of
// binary pixel data aborted the run - so the first time this test caught the bug
// it exists for, it reported a SIGABRT instead of the mismatch. A hex digest is
// short, printable, and identifies the card in the failure message.
QByteArray fingerprint(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    const QByteArray pixels(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());

    return QCryptographicHash::hash(pixels, QCryptographicHash::Md5).toHex();
}

}

// The wiring everything below rests on: that the card art reached the TEST
// binary at all. It is attached to the executable in CMakeLists.txt, and the
// tests needed a line of their own - without it every assertion in this file
// fails at once and for a reason that is nothing to do with CardRenderer.
//
// WHAT IS DELIBERATELY NOT ASSERTED HERE, because it was tried and it failed:
// that QPixmap(":/cards/ace_of_spades.svg") comes back null. It does not. The
// qsvg imageformat plugin is present in a normal Qt install - it is sitting in
// plugins/imageformats/libqsvg.so - so loading a card the obvious way WORKS on
// a development machine. It is the deploy step that drops the plugin
// (--skip-plugin-types imageformats, near the bottom of CMakeLists.txt), so the
// null pixmap section 18.1 warns about appears only in an installed build.
//
// That makes the trap worse than the documents describe, and worth writing down
// rather than only avoiding: a widget that reached for QPixmap directly would
// pass every test, look right on the machine it was written on, and ship cards
// that are blank for users. It cannot be caught here, because the suite would
// have to assert the ABSENCE of a plugin that is legitimately installed. The
// seam is what catches it - one place constructs a card pixmap, and it uses
// QSvgRenderer.
TEST_CASE("The card art is embedded, and its resource paths are flat", "[card-renderer]")
{
    QSvgRenderer renderer(QStringLiteral(":/cards/ace_of_spades.svg"));
    REQUIRE(renderer.isValid());

    // cards.qrc aliases every file to a flat name, so the png/ and svg/
    // directories the sources live in are NOT part of the runtime path. Getting
    // this wrong is the other way to build a filename that names nothing.
    //
    // Asked of the resource system rather than of QSvgRenderer: a renderer given
    // a path that does not exist is invalid for that reason and also for a
    // malformed file, so it would pass this while proving less - and it warns to
    // stderr on the way, which makes a passing suite look like a failing one.
    REQUIRE(QFile::exists(QStringLiteral(":/cards/ace_of_spades.svg")));
    REQUIRE_FALSE(QFile::exists(QStringLiteral(":/cards/svg/ace_of_spades.svg")));
}

TEST_CASE("Every one of the 52 cards renders", "[card-renderer]")
{
    const CardRenderer renderer;

    for(const Card& card : allCards())
    {
        INFO("card " << card.toString());
        REQUIRE_FALSE(renderer.face(card, QSize(41, 60), 1.0).isNull());
    }
}

// THE ONE THAT CATCHES A TRANSPOSED SWITCH ARM. Rendering non-null only proves
// the filename named a file that exists; every one of the 52 names does. Swap
// Rank::Jack and Rank::Queen, or Suit::Spades and Suit::Clubs, and every card
// still loads - two of them just load the same art as each other.
TEST_CASE("No two cards render the same art, and the back matches none of them",
          "[card-renderer]")
{
    const CardRenderer renderer;
    const QSize size(41, 60);

    QSet<QByteArray> seen;

    for(const Card& card : allCards())
    {
        INFO("card " << card.toString());

        const QByteArray pixels = fingerprint(renderer.face(card, size, 1.0));

        REQUIRE_FALSE(seen.contains(pixels));
        seen.insert(pixels);
    }

    REQUIRE(seen.size() == 52);
    REQUIRE_FALSE(seen.contains(fingerprint(renderer.back(size, 1.0))));
}

// The whole of the HiDPI story, in two assertions. Rasterizing at the logical
// size and letting Qt scale it up is what "cards soften on a HiDPI display"
// actually looks like from here.
TEST_CASE("A card is rasterized at the device pixel ratio and carries it", "[card-renderer]")
{
    const CardRenderer renderer;
    const QSize size(80, 116);

    const QPixmap pixmap = renderer.face(Card(Rank::Ace, Suit::Spades), size, 2.0);

    REQUIRE(pixmap.size() == QSize(160, 232));
    REQUIRE(pixmap.devicePixelRatio() == Catch::Approx(2.0));

    // And it still lays out as the size that was asked for, which is the point
    // of the ratio being stamped on rather than the caller being handed a bigger
    // card.
    REQUIRE(pixmap.deviceIndependentSize().toSize() == size);
}

// The build order's own symptom table lists this one: "cards soften on a HiDPI
// display - the pixmap cache is not keyed on dpr". Drop dpr from cacheKey() and
// the second call here is served the ratio-1 pixmap.
TEST_CASE("The cache is keyed on the device pixel ratio", "[card-renderer]")
{
    const CardRenderer renderer;
    const Card card(Rank::Ace, Suit::Spades);
    const QSize size(41, 60);

    const QPixmap standard = renderer.face(card, size, 1.0);
    const QPixmap retina = renderer.face(card, size, 2.0);

    REQUIRE(standard.cacheKey() != retina.cacheKey());
    REQUIRE(retina.size() == QSize(82, 120));
}

// AND FINELY ENOUGH FOR A SCALED VIEW. Since the table redesign's phase 2 the ratio
// a card asks for is the screen's times the view's scale, which is continuous -
// not the 1, 1.25, 1.5 or 2 a screen reports. A key quantized to hundredths served
// one of these two the other's pixmap: 127 pixels tall where 125 were asked for,
// stamped with the wrong ratio, so it was resampled on screen and missed its own
// card rect by a pixel or two. These are the ratios a resize from one window size
// to the next really produces, for the hand's 225-unit card.
TEST_CASE("Ratios a hundredth apart are cached apart", "[card-renderer]")
{
    const CardRenderer renderer;
    const Card card(Rank::Ace, Suit::Spades);
    const QSize size(155, 225);

    REQUIRE(renderer.face(card, size, 0.5649).height() == 127);

    const QPixmap next = renderer.face(card, size, 0.5551);

    REQUIRE(next.height() == 125);
    REQUIRE(next.devicePixelRatio() == Catch::Approx(0.5551));

    REQUIRE(renderer.back(size, 0.5649).height() == 127);
    REQUIRE(renderer.back(size, 0.5551).height() == 125);
}

// The size half of the key, which the dpr test above does NOT cover: it varies
// the ratio at one size, so dropping the size from the key leaves it passing.
// The window is what sets this, so the cards would stop resizing with it - and
// they would stop at whatever size the first frame after launch asked for, which
// looks like a layout bug and is not one.
TEST_CASE("A card is rendered at every size it is asked for", "[card-renderer]")
{
    const CardRenderer renderer;
    const Card card(Rank::King, Suit::Clubs);

    // ONE DIMENSION AT A TIME, which is the whole design of this test. Two sizes
    // differing in BOTH would be separated by either half of the key on its own,
    // so dropping one and keeping the other would pass - and that was the first
    // version of this test, which a mutation run caught doing exactly that.
    //
    // Same width, different height: only the height can tell these apart.
    REQUIRE(renderer.face(card, QSize(41, 60), 1.0).size() == QSize(41, 60));
    REQUIRE(renderer.face(card, QSize(41, 90), 1.0).size() == QSize(41, 90));

    // Same height, different width: only the width can.
    REQUIRE(renderer.face(card, QSize(60, 60), 1.0).size() == QSize(60, 60));

    // And the first size again, to prove what was stored under it survived.
    REQUIRE(renderer.face(card, QSize(41, 60), 1.0).size() == QSize(41, 60));

    REQUIRE(renderer.back(QSize(41, 60), 1.0).size() == QSize(41, 60));
    REQUIRE(renderer.back(QSize(41, 90), 1.0).size() == QSize(41, 90));
    REQUIRE(renderer.back(QSize(60, 60), 1.0).size() == QSize(60, 60));
}

// cacheKey() is identity, not equality: it says the two pixmaps share their
// pixels, which distinguishes "the cache returned what it stored" from "a second
// rasterization happened to produce the same image".
TEST_CASE("A repeated request is served from the cache", "[card-renderer]")
{
    const CardRenderer renderer;
    const Card card(Rank::Ten, Suit::Diamonds);
    const QSize size(41, 60);

    REQUIRE(renderer.face(card, size, 1.0).cacheKey() == renderer.face(card, size, 1.0).cacheKey());
    REQUIRE(renderer.back(size, 1.0).cacheKey() == renderer.back(size, 1.0).cacheKey());
}

// Without `state` in the key the first rendering of a card wins and the other
// one is never seen again - so a dimmed card would come back fully lit, or,
// worse, a legal card would come back dimmed for the rest of the game.
TEST_CASE("A dimmed card is not the same image as a lit one", "[card-renderer]")
{
    const CardRenderer renderer;
    const Card card(Rank::Queen, Suit::Hearts);
    const QSize size(41, 60);

    const QPixmap lit = renderer.face(card, size, 1.0, CardRenderer::State::Normal);
    const QPixmap dim = renderer.face(card, size, 1.0, CardRenderer::State::Dimmed);

    REQUIRE(fingerprint(lit) != fingerprint(dim));

    // And it is dimmer rather than merely different - the treatment is 45%
    // opacity, so the card's own body must have lost alpha. The centre of a
    // court card is inside the card body, where a lit card is fully opaque.
    const QImage dimImage = dim.toImage().convertToFormat(QImage::Format_ARGB32);
    REQUIRE(qAlpha(dimImage.pixel(dimImage.width() / 2, dimImage.height() / 2)) < 255);
}

TEST_CASE("A card's size comes from the art's aspect ratio", "[card-renderer]")
{
    const CardRenderer renderer;

    // The SVG viewBox is 167.0869141 x 242.6669922, so a 242.667-tall card is
    // 167.087 wide. A hardcoded 2:3 would give 161 here.
    REQUIRE(renderer.sizeForHeight(242.6669922) == QSize(167, 243));

    const QSize small = renderer.sizeForHeight(60.0);
    REQUIRE(small.height() == 60);
    REQUIRE(small.width() == 41);

    // A degenerate request is a null size rather than a crash or a 1x1 card.
    REQUIRE(renderer.sizeForHeight(0.0).isEmpty());
}

// THE REDESIGN'S BACK (table.js backSvg()), checked where its colours are flat: the
// cream border, the burgundy band between the border and the lattice, the gold
// diamond in the middle, and the burgundy oval round it. At twice the mock-up's
// 100 x 145 viewBox, so every point below is backSvg()'s coordinate doubled.
TEST_CASE("The back is the mock-up's burgundy and gold", "[card-renderer]")
{
    const CardRenderer renderer;
    const QImage back = renderer.back(QSize(200, 290), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);

    const auto near = [&](QPoint at, QRgb wanted)
    {
        const QRgb got = back.pixel(at);

        INFO("at " << at.x() << ", " << at.y() << ": got " << std::hex << got << " wanted " << wanted);

        return qAlpha(got) == 255 && std::abs(qRed(got) - qRed(wanted)) <= 3
               && std::abs(qGreen(got) - qGreen(wanted)) <= 3
               && std::abs(qBlue(got) - qBlue(wanted)) <= 3;
    };

    CHECK(near(QPoint(2, 145), qRgb(0xF2, 0xE8, 0xD2)));      // the border, x 1
    CHECK(near(QPoint(12, 145), qRgb(0x7A, 0x1C, 0x1C)));     // the field, x 6
    CHECK(near(QPoint(100, 145), qRgb(0xD8, 0xB5, 0x6A)));    // the diamond, at the centre
    CHECK(near(QPoint(100, 175), qRgb(0x7A, 0x1C, 0x1C)));    // the oval, below the diamond

    // And the lattice is there at all: its region holds more than one colour.
    QSet<QRgb> colours;
    for(int x = 20 ; x < 60 ; x++)
        colours.insert(back.pixel(x, 40));

    CHECK(colours.size() > 3);
}
