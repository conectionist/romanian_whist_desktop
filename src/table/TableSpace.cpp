#include "TableSpace.h"

#include <QtMath>

#include <algorithm>

qreal tablespace::fitScale(QSizeF viewport)
{
    const qreal width = viewport.width() - 2.0 * marginX;
    const qreal height = viewport.height() - marginTop - marginBottom;

    if(width <= 0.0 || height <= 0.0)
        return 0.0;

    return std::min(width / fitBox.width(), height / fitBox.height());
}

QSize tablespace::minimumViewport()
{
    return QSize(qCeil(fitBox.width() * minimumScale + 2.0 * marginX),
                 qCeil(fitBox.height() * minimumScale + marginTop + marginBottom));
}

qreal tablespace::playedCardHeight(std::size_t seatCount)
{
    return seatCount <= 4 ? 205.2 : 181.2;
}
