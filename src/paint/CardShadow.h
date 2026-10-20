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

// WHAT A CARD CASTS: on the felt (the table redesign's section 2.7), page.html's
// .tcard .inner box-shadows, and in your hand (section 2.8), .hcard .face's - each
// drawn as one picture under the card.
//
// Every played card casts a soft shadow. The winning one casts a deeper one, and
// glows inside a gold-bright ring - all three of them CSS box-shadows on the same
// element, so all three are here, in CSS's order: the first listed on top. A card
// in your hand casts a shadow of its own, down and to the left; raised under the
// cursor, it glows inside the same ring.
//
// IN THE CARD'S OWN FRAME, centred on its middle as CardItem is. The shadow falls
// toward the card's own bottom, wherever the card is turned - CSS draws a
// box-shadow before it applies the element's rotation - so the top seat's falls
// up the screen. The racks' do the same.
//
// A PICTURE AND NOT A PAINT, for the reason a rack is one: a blur is too dear for
// every frame of a window being dragged. Each look and card size is one picture,
// turned into place by every card that casts it.
namespace cardshadow
{

// WHICH OF FOUR LOOKS a card casts: on the felt, plain or the trick's winner
// (.tcard .inner, .tcard.winning .inner); in your hand, at rest or raised under the
// cursor (.hcard .face, .hcard.playable:hover .face).
enum class Look
{
    Played,
    Winning,
    Held,
    Raised
};

// How far a card rises toward its own top: .tcard.winning .inner's translateY(-8px)
// and the hovered hand card's translateY(-30px). The shadow, the glow and the ring
// rise with it, as they are box-shadows on the element that moves.
inline constexpr qreal winningLift = 8.0;
inline constexpr qreal raisedLift = 30.0;

constexpr qreal lift(Look look)
{
    return look == Look::Winning ? winningLift : look == Look::Raised ? raisedLift : 0.0;
}

// How far the picture reaches past the card, in its frame, before the lift - so
// CardItem's rect covers it.
QMarginsF reach(Look look);

// `offset` is where its top-left goes in the card's frame: a whole number of
// pixels at its ratio from the card's own top-left, so the two can be laid on one
// pixel.
using Picture = devicepixels::Canvas;

// THE COLOURS A CARD CASTS, out of the table's hundred. What a card keeps and what
// the cache compares on every request, so neither carries nor compares the rest of
// the palette.
struct Colours
{
    QColor shadow;
    QColor winningShadow;
    QColor winningGlow;
    QColor ring;
    QColor heldShadow;
    QColor raisedShadow;
    QColor raisedGlow;

    bool operator==(const Colours&) const = default;
};

Colours colours(const Theme::TablePalette& palette);

// The picture for a card of `size`, in table units, at `ratio`, before the lift.
// Null for an empty card or ratio.
Picture render(QSizeF size, Look look, const Colours& colours, qreal ratio);

// render()'S PICTURES, SHARED by every card on the table and in your hand. One ratio
// and one palette at a time, as panel::Cache and rackitem::Cache hold theirs: a new
// ratio means the window was resized or moved, and the old pictures are never asked
// for again.
//
// Owned by whatever owns the cards (TableScene), which outlives them.
class Cache
{
public:
    Picture picture(QSizeF size, Look look, const Colours& colours, qreal ratio);

    // How many pictures this has rendered: READ-ONLY and for tests.
    std::size_t renders() const { return pictures.renders(); }

private:
    struct Key
    {
        QSizeF size;
        Look look = Look::Played;

        bool operator==(const Key&) const = default;
    };

    // One played size and two looks on the felt, one card size and two looks in
    // your hand: four pictures, and room for a played size from the last game.
    PictureCache<Key, Colours, 8> pictures;
};

} // namespace cardshadow

#endif
