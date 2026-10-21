#ifndef ICONS_H
#define ICONS_H

#include <QColor>
#include <QRectF>

class QPainter;

// THE MOCK-UP'S LINE ICONS, page.html's inline SVGs: drawn on a 24-unit grid with
// a stroke 1.8 wide and no fill, in whatever colour the text beside them is. The
// two corner buttons' (section 2.11) and the menu bar's four (section 2.12).
//
// AS VECTORS, through QSvgRenderer, into whatever painter is handed over - so an
// icon is as sharp as the picture it is drawn into, at any scale.
namespace icons
{

enum class Icon
{
    // A ruled grid: Round history.
    RoundHistory,

    // Two cards, one behind the other: Last trick.
    LastTrick,

    // The menu bar's (section 2.12), left to right.

    // A plus in a circle: New Game.
    NewGame,

    // A cog drawn as a hub and eight spokes: Settings.
    Settings,

    // An open book: Rules.
    Rules,

    // A door frame with an arrow leaving it: Exit.
    Exit
};

// Paints `icon` filling `box`, its strokes in `colour`.
void paint(QPainter& painter, Icon icon, const QRectF& box, const QColor& colour);

} // namespace icons

#endif
