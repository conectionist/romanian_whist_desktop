#ifndef PICTURE_MEMO_H
#define PICTURE_MEMO_H

#include "paint/DevicePixels.h"

#include <QtGlobal>

#include <cstddef>
#include <utility>

// ONE ITEM'S PICTURE, KEPT between paints: the name panels', the racks', the
// played cards' shadows and the lead rhombus's. Each item paints a picture it
// composes, or asks a PictureCache for, and keeps it until what it shows changes
// (invalidate()) or it is painted at another ratio, which changes without any
// setter - a window resized, or moved to another screen.
//
// ONE COPY OF WHAT FOUR ITEMS EACH KEPT FOR THEMSELVES (PHASE5_REVIEW_FINDINGS.md
// #12), which had already drifted apart: SeatItem's alone refused to keep a null
// picture as current, and the rack's and the card shadow's did keep one (#3) - a
// rack with cards could go on drawing nothing until its next change.
class PictureMemo
{
public:
    // What the picture shows has changed: the next request composes it again.
    void invalidate() { stale = true; }

    // The picture at `ratio`: the one kept, unless it is out of date or was made at
    // another ratio, and then `compose()`'s.
    //
    // A NULL PICTURE IS NEVER KEPT AS CURRENT. It is returned, and the memo stays
    // out of date, so the next paint composes rather than drawing nothing for good.
    template<typename Compose>
    const devicepixels::Canvas& picture(qreal ratio, Compose&& compose)
    {
        const int key = devicepixels::ratioKey(ratio);

        if(!stale && key == ratioKey && !held.pixmap.isNull())
            return held;

        held = std::forward<Compose>(compose)();
        ratioKey = key;
        stale = held.pixmap.isNull();

        if(!stale)
            composed++;

        return held;
    }

    // How many pictures this has made: READ-ONLY and for tests, which show with it
    // that a resize settling, or a snapshot that changes nothing, leaves an item's
    // picture alone.
    std::size_t compositions() const { return composed; }

private:
    devicepixels::Canvas held;
    int ratioKey = 0;
    bool stale = true;
    std::size_t composed = 0;
};

#endif
