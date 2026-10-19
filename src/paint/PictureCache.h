#ifndef PICTURE_CACHE_H
#define PICTURE_CACHE_H

#include "paint/DevicePixels.h"

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
// Within one, a picture per key, found by a linear search - there are a handful -
// and the set starts again at `Most`, since more means keys that will not come
// back are being kept.
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

        for(const Entry& entry : entries)
        {
            if(entry.key == key)
                return entry.picture;
        }

        if(entries.size() >= Most)
            entries.clear();

        entries.push_back({ key, std::forward<Render>(render)() });
        rendered++;

        return entries.back().picture;
    }

    // How many pictures this has rendered: READ-ONLY and for tests.
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
