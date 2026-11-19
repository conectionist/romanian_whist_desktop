#ifndef ICONS_H
#define ICONS_H

#include <QColor>
#include <QRectF>

class QPainter;

// The mock-up's line icons, page.html's inline SVGs, for the corner buttons and the menu
// bar's menus: a 24-unit grid, strokes 1.8 wide and no fill, in the colour of the text
// beside them.
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

    // The menu bar's menus'.

    // A plus in a circle: New Game.
    NewGame,

    // A cog drawn as a hub and eight spokes: Settings.
    Settings,

    // An open book: Rules.
    Rules,

    // A door frame with an arrow leaving it: Quit, here and on the menu page.
    Exit,

    // Two upright bars: Pause.
    Pause
};

// Paints `icon` filling `box`, its strokes in `colour`.
void paint(QPainter& painter, Icon icon, const QRectF& box, const QColor& colour);

} // namespace icons

#endif
