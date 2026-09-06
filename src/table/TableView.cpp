#include "TableView.h"

#include "table/TableScene.h"

#include <QResizeEvent>

TableView::TableView(TableScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , table(scene)
{
    // Nothing scrolls: the scene is exactly the viewport, always. Scrollbars
    // would also change the viewport size when they appeared, which is a resize
    // that causes a relayout that can bring them back.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);

    // The background comes from Theme through TableScene::drawBackground(), so
    // the viewport must not paint its own beneath it.
    setBackgroundBrush(Qt::NoBrush);
    viewport()->setAutoFillBackground(false);

    // A card moving in the middle of the table repaints the table. The default
    // MinimalViewportUpdate leaves seams behind an item whose bounding rect
    // changes, and the table is small enough that the whole viewport is cheap.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // Anchored at the top left rather than the centre: with the scene rect
    // pinned to the viewport there is nothing to keep centred, and
    // AnchorViewCenter would fight the resize below.
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
}

void TableView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    if(!table)
        return;

    // THE VIEWPORT, not the widget: they differ by the frame, and a scene rect
    // set from the widget puts the bottom of the table under the edge.
    table->setSceneRect(QRectF(QPointF(0.0, 0.0), viewport()->size()));
    table->relayout();
}
