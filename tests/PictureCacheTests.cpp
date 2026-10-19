#include <catch2/catch_test_macros.hpp>

#include "paint/PictureCache.h"

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
