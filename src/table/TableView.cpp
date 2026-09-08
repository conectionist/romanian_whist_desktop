#include "TableView.h"

#include "table/TableScene.h"

#include <QResizeEvent>

#include <algorithm>

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

void TableView::setBottomOverlay(QWidget* overlay)
{
    bottomOverlay = overlay;

    if(!bottomOverlay)
        return;

    // REPARENTED HERE rather than trusted to have been built with this as its
    // parent. An overlay parented to the window would be positioned in this
    // widget's coordinates and drawn in the window's, which puts it somewhere
    // near the table by luck and nowhere near it after a resize.
    bottomOverlay->setParent(this);

    // Above the viewport, which is itself a child widget. Without this the
    // strip is behind the green and invisible.
    bottomOverlay->raise();

    // So a prompt that changes the strip's size moves it - see eventFilter().
    bottomOverlay->installEventFilter(this);

    placeOverlay();
}

void TableView::placeOverlay()
{
    if(!bottomOverlay)
        return;

    // ITS OWN PREFERRED HEIGHT, and no wider than the viewport. The strip knows
    // how tall it needs to be - the buttons and, when there is one, the
    // explanation beneath them - and this knows how much room there is.
    const QSize viewportSize = viewport()->size();
    const QSize hint = bottomOverlay->sizeHint();

    const int width = std::min(hint.width(), viewportSize.width());
    const int height = std::min(hint.height(), viewportSize.height());

    // ASKED OF THE SCENE, not measured off the viewport. The bottom of the table
    // is where the HUMAN'S PLAQUE is - seatlayout puts the seated player at
    // bottom-centre - so a strip pinned to the viewport's bottom edge lands on
    // top of it. That is not a hypothetical: it is what C2's first run drew.
    const QRectF band = table ? table->overlayBand() : QRectF();

    // Bottom-aligned within the band, so the strip sits just above the lowest
    // plaque and grows UPWARD over the middle of the table. That is deliberate:
    // there is no gap between the trick and the bottom plaque to fit a strip in
    // - thirteen pixels on a 1000x700 table - and bidding finishes before the
    // round's first card is played, so the middle is empty while this is up.
    //
    // The fallback is the viewport's own bottom, which is only reached before
    // the first snapshot or on a table too short to have a band - and at that
    // point there is no plaque drawn to collide with anyway.
    const int bottom = band.isEmpty() ? viewportSize.height()
                                      : static_cast<int>(band.bottom());

    bottomOverlay->setGeometry((viewportSize.width() - width) / 2,
                               std::max(0, bottom - height),
                               width,
                               height);
}

bool TableView::eventFilter(QObject* watched, QEvent* event)
{
    // THE STRIP CHANGES SIZE WITH THE PROMPT - an eight-card round has nine
    // buttons and a one-card round two, and the explanation appears only for the
    // final bidder. Geometry set once at setBottomOverlay() would therefore be
    // right for one round and wrong for the rest, and a window nobody resizes
    // would never correct it.
    //
    // Show as well as LayoutRequest, because the strip is hidden between turns
    // and its size hint is only settled once the new buttons are in place.
    if(watched == bottomOverlay &&
       (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show))
    {
        placeOverlay();
    }

    return QGraphicsView::eventFilter(watched, event);
}

void TableView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    if(table)
    {
        // THE VIEWPORT, not the widget: they differ by the frame, and a scene
        // rect set from the widget puts the bottom of the table under the edge.
        table->setSceneRect(QRectF(QPointF(0.0, 0.0), viewport()->size()));
        table->relayout();
    }

    // AFTER the relayout above, and the order is load-bearing: the overlay is
    // placed against overlayBand(), which is derived from the seat positions the
    // relayout has just recomputed. Placed first, it would be positioned against
    // the PREVIOUS size on every resize - correct only by one frame's delay, and
    // visibly wrong while a window is being dragged.
    placeOverlay();
}
