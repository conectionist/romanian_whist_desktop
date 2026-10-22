#include "ItemCursor.h"

#include <QCursor>

void itemcursor::show(QGraphicsItem& item, std::optional<Qt::CursorShape> shape)
{
    if(shape)
    {
        if(item.hasCursor() && item.cursor().shape() == *shape)
            return;

        item.setCursor(*shape);
        return;
    }

    if(!item.hasCursor())
        return;

    // See the header: the arrow first, so the view shows it now.
    item.setCursor(Qt::ArrowCursor);
    item.unsetCursor();
}
