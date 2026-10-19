#ifndef CARD_SHADOW_H
#define CARD_SHADOW_H

#include "paint/Theme.h"

#include <QMarginsF>
#include <QPixmap>
#include <QPointF>
#include <QSizeF>

#include <cstddef>
#include <vector>

// WHAT A CARD ON THE FELT CASTS (the table redesign's section 2.7): page.html's
// .tcard .inner box-shadows, drawn as one picture under the card.
//
// Every played card casts a soft shadow. The winning one casts a deeper one, and
// glows inside a gold-bright ring - all three of them CSS box-shadows on the same
// element, so all three are here, in CSS's order: the first listed on top.
//
// IN THE CARD'S OWN FRAME, centred on its middle as CardItem is. The shadow falls
// toward the card's own bottom, wherever the card is turned - CSS draws a
// box-shadow before it applies the element's rotation - so the top seat's falls
// up the screen. The racks' do the same.
//
// A PICTURE AND NOT A PAINT, for the reason a rack is one: a blur is too dear for
// every frame of a window being dragged, and six cards of two sizes are at most
// four pictures, turned into place by their items.
namespace cardshadow
{

// How far the winning card rises toward its own top: .tcard.winning .inner's
// translateY(-8px). The shadow, the glow and the ring rise with it.
inline constexpr qreal winningLift = 8.0;

// How far the picture reaches past the card, in its frame, for the plain shadow or
// the winning one - so CardItem's rect covers it.
QMarginsF reach(bool winning);

struct Picture
{
    QPixmap pixmap;

    // Where its top-left goes in the card's frame, on a whole pixel at its ratio.
    QPointF offset;
};

// The picture for a card of `size`, in table units, at `ratio`. Null for an empty
// card or ratio.
Picture render(QSizeF size, bool winning, const Theme::TablePalette& palette, qreal ratio);

// render()'S PICTURES, SHARED by every card on the table. One ratio and one palette
// at a time, as panel::Cache and rackitem::Cache hold theirs: a new ratio means the
// window was resized or moved, and the old pictures are never asked for again.
//
// Owned by whatever owns the cards (TableScene), which outlives them.
class Cache
{
public:
    Picture picture(QSizeF size, bool winning, const Theme::TablePalette& palette, qreal ratio);

    // How many pictures this has rendered: READ-ONLY and for tests.
    std::size_t renders() const { return rendered; }

private:
    struct Entry
    {
        QSizeF size;
        bool winning = false;
        Picture picture;
    };

    int ratioKey = 0;
    Theme::TablePalette palette;
    std::vector<Entry> entries;
    std::size_t rendered = 0;
};

} // namespace cardshadow

#endif
