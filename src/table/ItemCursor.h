#ifndef ITEM_CURSOR_H
#define ITEM_CURSOR_H

#include <QGraphicsItem>
#include <Qt>

#include <optional>

// THE CURSOR OVER A TABLE ITEM (the redesign's phase 12): page.html's `cursor:
// pointer` and `not-allowed`, shown by the item that knows which part of it is under
// the mouse - a card in your fan, a bid on the strip.
namespace itemcursor
{

// Shows `shape` over `item`, or with none the view's own arrow, AT ONCE: under a
// mouse that has not moved, too.
//
// QGraphicsItem::unsetCursor() alone does not. It puts the view's cursor back only
// when the item is itself the topmost item under the mouse, which a fan never is
// over one of its cards - they are its children, drawn above it - so the pointing
// hand stayed up after your turn ended until the mouse moved (PR #38's review,
// finding #4). setCursor() looks through every item under the mouse instead, so
// setting the arrow first shows it whatever is on top; unsetCursor() then drops it.
// The arrow is what the view goes back to: TableView sets no cursor of its own.
//
// NOTHING IS DONE when the item already shows what is asked: every setCursor() has
// Qt look for the items under the mouse again, and a hover asks on every move
// (finding #6).
//
// An item about to be hidden is cleared before it is hidden: hiding one never
// changes the view's cursor.
void show(QGraphicsItem& item, std::optional<Qt::CursorShape> shape);

} // namespace itemcursor

#endif
