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

// Shows `shape` over `item`, or with none the arrow, AT ONCE: under a mouse that has
// not moved, too. Every cursor a table item shows goes through here.
//
// NEVER QGraphicsItem::unsetCursor(), for two reasons Qt does not document:
//
// - It puts the view's cursor back only when the item is itself the topmost item
//   under the mouse, which a fan never is over one of its cards - they are its
//   children, drawn above it - so the pointing hand stayed up after your turn ended
//   until the mouse moved (PR #38's review, finding #4).
// - When the item IS topmost, it has the view look again under the view's last
//   mouse event - and a view under a mouse that has not moved since it appeared has
//   none, and Qt dereferences it all the same: a crash (found after that review).
//   The app does not reach it: an item gets a cursor only from a hover or a press,
//   which reach the scene through the view, and the view keeps that event first.
//   The tests did, sending a press straight to the scene under a mouse an earlier
//   test had left over the window. (Checked in the running app: a bid made by key
//   and by click, with the table brought up under a mouse resting on it, did not
//   crash.) Nothing here relies on that holding.
//
// So "none" is the arrow, set like any other cursor. setCursor() looks through
// every item under the mouse's real position, which needs no mouse event, and shows
// the first cursor it finds - this item's arrow, or the cursor of an item above it.
// The item keeps the arrow as its own: hasCursor() stays true, and cursor() is the
// arrow either way.
//
// NOTHING IS DONE when the item already shows what is asked: every setCursor() has
// Qt look for the items under the mouse again, and a hover asks on every move
// (finding #6).
//
// An item about to be hidden is given the arrow before it is hidden: hiding one
// never changes the view's cursor.
void show(QGraphicsItem& item, std::optional<Qt::CursorShape> shape);

} // namespace itemcursor

#endif
