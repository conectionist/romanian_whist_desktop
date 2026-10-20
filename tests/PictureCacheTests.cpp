#include <catch2/catch_test_macros.hpp>

#include "paint/PictureCache.h"
#include "paint/PictureMemo.h"

#include <QPixmap>

// THE ONE CACHE POLICY behind the panels', the racks' and the played cards'
// pictures: one ratio and one style at a time, a picture per key, and - since
// PHASE5_REVIEW_FINDINGS.md #10 - only the oldest dropped when it is full, where it
// used to clear everything and draw again what it had just been asked for.
namespace
{

using Cache = PictureCache<int, int, 3>;

// A picture to hold, told apart by its size; the key is what tells the cache.
devicepixels::Canvas pictureOf(int key)
{
    return { QPixmap(key, key), QPointF() };
}

// Asks `cache` for `key`, drawing it only if it has to.
void ask(Cache& cache, int key, qreal ratio = 1.0, int style = 0)
{
    cache.picture(key, style, ratio, [key] { return pictureOf(key); });
}

}

TEST_CASE("A picture asked for again is not drawn again", "[picture-cache]")
{
    Cache cache;

    ask(cache, 1);
    ask(cache, 1);

    REQUIRE(cache.renders() == 1);
}

TEST_CASE("A full cache drops only its oldest picture", "[picture-cache]")
{
    Cache cache;

    ask(cache, 1);
    ask(cache, 2);
    ask(cache, 3);
    REQUIRE(cache.renders() == 3);

    // Full: the fourth drops the first, and nothing else.
    ask(cache, 4);
    REQUIRE(cache.renders() == 4);

    ask(cache, 2);
    ask(cache, 3);
    ask(cache, 4);
    REQUIRE(cache.renders() == 4);

    // The first was the one that went.
    ask(cache, 1);
    REQUIRE(cache.renders() == 5);
}

// PHASE5_REVIEW_FINDINGS.md #13: the cache held a null picture as it held any
// other, so the item asking for it was handed the null on every later paint too.
TEST_CASE("A null picture is never held", "[picture-cache]")
{
    Cache cache;
    int calls = 0;

    const auto nullFirst = [&calls]
    {
        calls++;
        return calls == 1 ? devicepixels::Canvas{} : pictureOf(1);
    };

    REQUIRE(cache.picture(1, 0, 1.0, nullFirst).pixmap.isNull());

    // Asked again, it renders again rather than finding the null.
    REQUIRE_FALSE(cache.picture(1, 0, 1.0, nullFirst).pixmap.isNull());
    REQUIRE(calls == 2);

    // Only the picture it holds is counted, and it is held.
    REQUIRE(cache.renders() == 1);
    cache.picture(1, 0, 1.0, nullFirst);
    REQUIRE(calls == 2);
}

TEST_CASE("A new ratio or style starts the cache again", "[picture-cache]")
{
    Cache cache;

    ask(cache, 1);
    ask(cache, 1, 2.0);
    REQUIRE(cache.renders() == 2);

    ask(cache, 1, 2.0, 7);
    REQUIRE(cache.renders() == 3);

    // And the pictures of the old ratio are gone with it.
    ask(cache, 1, 1.0, 7);
    REQUIRE(cache.renders() == 4);
}

// ---- one item's picture, kept between paints (PHASE5_REVIEW_FINDINGS.md #12) ----
//
// The panels, the racks, the played cards' shadows and the lead rhombus each kept
// a copy of this, and the copies had drifted: two of them kept a null picture as
// current (#3), so an item with something to show could go on drawing nothing.

namespace
{

// Composes into `memo` at `ratio`, from `picture`, and counts the calls.
struct Composer
{
    PictureMemo memo;
    int calls = 0;

    const devicepixels::Canvas& at(qreal ratio, const QPixmap& picture = QPixmap(4, 4))
    {
        return memo.picture(ratio, [&]
        {
            calls++;
            return devicepixels::Canvas{ picture, QPointF() };
        });
    }
};

}

TEST_CASE("A kept picture is not composed again until it changes", "[picture-memo]")
{
    Composer item;

    item.at(1.0);
    item.at(1.0);
    REQUIRE(item.calls == 1);
    REQUIRE(item.memo.compositions() == 1);

    // What it shows changed.
    item.memo.invalidate();
    item.at(1.0);
    REQUIRE(item.calls == 2);

    // Painted at another ratio, which no setter announces.
    item.at(2.0);
    REQUIRE(item.calls == 3);
    REQUIRE(item.memo.compositions() == 3);
}

TEST_CASE("A null picture is never kept as current", "[picture-memo]")
{
    Composer item;

    // Nothing to show this time - an allocation that failed, or no chrome yet.
    REQUIRE(item.at(1.0, QPixmap()).pixmap.isNull());

    // The next paint composes again, rather than drawing nothing for good...
    REQUIRE_FALSE(item.at(1.0).pixmap.isNull());
    REQUIRE(item.calls == 2);

    // ...and only what was made is counted.
    REQUIRE(item.memo.compositions() == 1);

    // Then it is kept as usual.
    item.at(1.0);
    REQUIRE(item.calls == 2);
}

// PHASE5_REVIEW_FINDINGS.md #14: a null composed at a degenerate ratio replaced the
// picture kept, so the next paint at the real ratio composed it all again.
TEST_CASE("A null picture leaves the one kept alone", "[picture-memo]")
{
    Composer item;

    item.at(1.0);
    REQUIRE(item.at(0.0, QPixmap()).pixmap.isNull());

    // Back at the ratio it was made at, the kept picture is still current.
    REQUIRE_FALSE(item.at(1.0).pixmap.isNull());
    REQUIRE(item.calls == 2);

    // But an out-of-date picture is never handed back: after a change and a
    // null, the next paint composes.
    item.memo.invalidate();
    REQUIRE(item.at(1.0, QPixmap()).pixmap.isNull());
    item.at(1.0);
    REQUIRE(item.calls == 4);
    REQUIRE(item.memo.compositions() == 2);
}
