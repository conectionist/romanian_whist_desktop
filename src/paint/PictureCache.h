#ifndef PICTURE_CACHE_H
#define PICTURE_CACHE_H

#include "paint/DevicePixels.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

// PICTURES SHARED BY EVERY ITEM THAT DRAWS THE SAME ONE: the name panels', the
// racks' and the played cards' shadows, each keyed by what tells its pictures
// apart and rendered by its own function.
//
// ONE RATIO AND ONE STYLE AT A TIME. A new ratio means the window was resized or
// moved to another screen, and a new style a theme change; either way the old
// pictures are never asked for again, so they are dropped rather than kept.
// Within one, a picture per key, found by a linear search - there are a handful.
//
// AT `Most`, THE LEAST RECENTLY ASKED FOR GOES, and only it. Clearing them all made
// a full cache draw again pictures it had just been asked for: a six-player
// Forehead round's faces cleared every rack back a round had drawn
// (PHASE5_REVIEW_FINDINGS.md #10). Dropping the oldest drawn did the same more
// slowly: the backs, asked for on every frame, went before faces never seen again
// (#15).
//
// BY VALUE, which costs a reference count: a reference into the cache would
// dangle the moment another item's request cleared it.
template<typename Key, typename Style, std::size_t Most>
class PictureCache
{
public:
    // The picture for `key` at `ratio` in `style`, from `render()` if it is not
    // held already.
    template<typename Render>
    devicepixels::Canvas picture(const Key& key, const Style& style, qreal ratio, Render&& render)
    {
        const int wanted = devicepixels::ratioKey(ratio);

        if(wanted != ratioKey || !(style == held))
        {
            entries.clear();
            ratioKey = wanted;
            held = style;
        }

        for(auto entry = entries.begin() ; entry != entries.end() ; ++entry)
        {
            if(entry->key == key)
            {
                // To the back, the most recently asked for.
                std::rotate(entry, entry + 1, entries.end());
                return entries.back().picture;
            }
        }

        devicepixels::Canvas made = std::forward<Render>(render)();

        // A NULL PICTURE IS NEVER HELD, as PictureMemo never keeps one as current:
        // held, it would be found by every later request and the item would go on
        // drawing nothing (PHASE5_REVIEW_FINDINGS.md #13).
        if(made.pixmap.isNull())
            return made;

        if(entries.size() >= Most)
            entries.erase(entries.begin());

        entries.push_back({ key, std::move(made) });
        rendered++;

        return entries.back().picture;
    }

    // How many pictures this has rendered and holds, so not the null ones:
    // READ-ONLY and for tests.
    std::size_t renders() const { return rendered; }

private:
    struct Entry
    {
        Key key;
        devicepixels::Canvas picture;
    };

    int ratioKey = 0;
    Style held{};
    std::vector<Entry> entries;
    std::size_t rendered = 0;
};

#endif
