#ifndef CARD_SHADOW_H
#define CARD_SHADOW_H

#include "paint/PictureCache.h"
#include "paint/Theme.h"

#include <QColor>
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

// `offset` is where its top-left goes in the card's frame: a whole number of
// pixels at its ratio from the card's own top-left, so the two can be laid on one
// pixel.
using Picture = devicepixels::Canvas;

// THE FOUR COLOURS A PLAYED CARD CASTS, out of the table's hundred. What a card
// keeps and what the cache compares on every request, so neither carries nor
// compares the rest of the palette.
struct Colours
{
    QColor shadow;
    QColor winningShadow;
    QColor winningGlow;
    QColor ring;

    bool operator==(const Colours&) const = default;
};

Colours colours(const Theme::TablePalette& palette);

// The picture for a card of `size`, in table units, at `ratio`. Null for an empty
// card or ratio.
Picture render(QSizeF size, bool winning, const Colours& colours, qreal ratio);

// render()'S PICTURES, SHARED by every card on the table. One ratio and one palette
// at a time, as panel::Cache and rackitem::Cache hold theirs: a new ratio means the
// window was resized or moved, and the old pictures are never asked for again.
//
// Owned by whatever owns the cards (TableScene), which outlives them.
class Cache
{
public:
    Picture picture(QSizeF size, bool winning, const Colours& colours, qreal ratio);

    // How many pictures this has rendered: READ-ONLY and for tests.
    std::size_t renders() const { return pictures.renders(); }

private:
    struct Key
    {
        QSizeF size;
        bool winning = false;

        bool operator==(const Key&) const = default;
    };

    // Two card sizes and two states are four pictures.
    PictureCache<Key, Colours, 8> pictures;
};

} // namespace cardshadow

#endif
