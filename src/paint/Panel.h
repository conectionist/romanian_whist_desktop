#ifndef PANEL_H
#define PANEL_H

#include "paint/PictureCache.h"
#include "paint/Theme.h"

#include <QMarginsF>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <cstddef>
#include <vector>

class QPainter;

// THE REDESIGN'S PANEL, page.html's .panel, painted in one place: the seats' name
// panels from phase 4, the scoreboard, trick panel and buttons in phase 8, and the
// sheets in phase 10. Section 2.2: a vertical gradient, a gold hairline border, a
// dark inset line with a faint gold one inside it, and a drop shadow.
//
// AND THE ACTIVE SEAT'S DRESS (.plaque.active): a 3-unit gold-bright ring round
// the border and a soft gold glow round that. A panel that is never active simply
// never asks for it. And a button's under the cursor (.panel.btn:hover): a
// brighter inset line and a fainter glow of its own.
//
// IN TABLE UNITS, the painter's, like everything else on the table.
namespace panel
{

// The border box's corner radius.
inline constexpr qreal radius = 9.0;

// HOW THE PANEL IS DRESSED. One of three rather than two flags, because the mock-up
// has no panel that is both: a seat is never a button, and a button is never on
// turn.
enum class State
{
    Plain,

    // The seat whose turn it is: the ring and the glow.
    Active,

    // A button under the cursor: the inset gold line at gold-bright and a faint
    // glow, with no ring.
    Hovered
};

// HOW FAR PAST ITS RECT A PANEL PAINTS: its drop shadow, 12 units down and blurred
// 26, and an active panel's glow, blurred 26 all round. The same whatever its
// state, so an item's bounding rect does not change as the turn moves round the
// table or the cursor crosses a button.
QMarginsF reach();

// The panel's shape, which is also what should take the mouse.
QPainterPath shape(const QRectF& rect);

// Paints the panel filling `rect`, in CSS's paint order: the outer shadows (the
// drop shadow lowest, then the glow, then the ring), the background, the inset
// lines, and the border on top.
void paint(QPainter& painter, const QRectF& rect, const Theme::TablePalette& palette, State state);

// A PANEL AS A PICTURE, for an item that draws one every frame. The two blurred
// shadows cost about a millisecond a panel, which a window being dragged cannot
// afford six times a frame - so an item renders this once per size, state and
// ratio and blits it.
//
// `offset` is where the picture's top-left goes relative to the panel's: minus the
// reach, grown to a whole number of pixels at `ratio`, so that the panel's edges
// fall on the picture's pixel grid and its hairlines stay crisp.
using Picture = devicepixels::Canvas;

Picture render(QSizeF size, const Theme::TablePalette& palette, State state, qreal ratio);

// render()'S PICTURES, SHARED by every panel drawn at the same size, state, ratio and
// palette. Five of six seats are the same 280 x 118 at the same ratio, so a cache
// per item blurred the same two shadows over and over on every settle.
//
// ONE RATIO AND ONE PALETTE AT A TIME, as Theme's avatars are one box at a time:
// a new ratio means the window was resized or moved to another screen, and the
// old pictures are never asked for again. Within one, a picture per size and
// state - the bottom panel's two and everyone else's, the scoreboard's and the
// trick panel's, and the two buttons' in each of their states.
//
// Owned by whatever owns the items (TableScene), which outlives them.
class Cache
{
public:
    // BY VALUE, which costs a reference count: a reference into the cache would
    // dangle the moment another panel's request cleared it.
    Picture picture(QSizeF size, const Theme::TablePalette& palette, State state, qreal ratio);

    // How many pictures this has rendered: READ-ONLY and for tests.
    std::size_t renders() const { return pictures.renders(); }

private:
    struct Key
    {
        QSizeF size;
        State state = State::Plain;

        bool operator==(const Key&) const = default;
    };

    // A handful of sizes is every panel there is: four for the seats, two for the
    // corner panels - whose heights follow what they say, so an old height or two
    // lingers - and four for the buttons.
    PictureCache<Key, Theme::TablePalette, 16> pictures;
};

} // namespace panel

#endif
