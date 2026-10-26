#include "ItemCursor.h"

#include <QCursor>

void itemcursor::show(QGraphicsItem& item, std::optional<Qt::CursorShape> shape)
{
    const Qt::CursorShape wanted = shape.value_or(Qt::ArrowCursor);

    // NOTHING TO DO: the item already asks for this, or asks for nothing and the arrow
    // is wanted - which is what a hover over a gap asks on every move.
    if(item.cursor().shape() == wanted && (item.hasCursor() || wanted == Qt::ArrowCursor))
        return;

    // NEVER unsetCursor(): see the header. The arrow is a cursor like any other.
    item.setCursor(wanted);
}
