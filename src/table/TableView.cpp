#include "TableView.h"

#include "table/TableScene.h"
#include "table/TableSpace.h"

#include <QRect>
#include <QResizeEvent>
#include <QTransform>

#include <algorithm>

TableView::TableView(TableScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , table(scene)
{
    // Nothing scrolls: the scaled table always fits inside the viewport. Scrollbars
    // would also change the viewport size when they appeared, which is a resize
    // that rescales the table and can bring them back.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);

    // The room comes from Theme through TableScene::drawBackground(), which covers
    // the whole viewport, so the viewport must not paint its own beneath it.
    setBackgroundBrush(Qt::NoBrush);
    viewport()->setAutoFillBackground(false);

    // A card moving in the middle of the table repaints the table. The default
    // MinimalViewportUpdate leaves seams behind an item whose bounding rect
    // changes, and the table is small enough that the whole viewport is cheap.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // CENTRED BY THE ALIGNMENT, not by scrolling. fitTable() leaves a margin round
    // the scaled scene on every side, so it is always smaller than the viewport,
    // QGraphicsView never has a scroll range, and the alignment alone puts the
    // table in the middle - exactly, with no scroll value to round. The anchors
    // are then moot, and NoAnchor says so rather than leaving a default to
    // wonder about.
    setAlignment(Qt::AlignCenter);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
}

// The view has no frame and no scrollbars, so its own size is the viewport's.
QSize TableView::minimumSizeHint() const
{
    return tablespace::minimumViewport();
}

// table.js fit(): one scale for both axes, so the table stays round, and the
// largest that fits the fit box inside the viewport's margins.
//
// SET BY HAND RATHER THAN THROUGH fitInView(), which would get the same answer
// less a 2-pixel margin of its own that nothing in the mock-up has.
void TableView::fitTable()
{
    const qreal scale = tablespace::fitScale(viewport()->size());

    // A viewport with no room inside its margins - a window being created, or one
    // squashed to nothing. The last scale stays rather than a zero one that would
    // make the transform singular.
    if(scale <= 0.0)
        return;

    setTransform(QTransform::fromScale(scale, scale));
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

    // NO WIDER THAN THE VIEWPORT. The strip knows how wide it would like to be -
    // the buttons, side by side - and this knows how much room there is.
    const QSize viewportSize = viewport()->size();
    const QSize hint = bottomOverlay->sizeHint();

    const int width = std::min(hint.width(), viewportSize.width());

    // THEN THE HEIGHT AT THE WIDTH IT IS ACTUALLY GETTING, not at the one it
    // asked for. The explanation beneath the buttons is word-wrapped, so a strip
    // squeezed narrower than its hint needs a second line for it - and
    // sizeHint().height() reports the height of the first line only. Taking the
    // hint's height there is what clipped the forbidden-bid reason in half on a
    // window narrow enough that the scoreboard crowds the table.
    const int hfw = bottomOverlay->hasHeightForWidth() ? bottomOverlay->heightForWidth(width)
                                                       : -1;

    const int preferred = hfw > 0 ? hfw : hint.height();

    // ASKED OF THE SCENE, not measured off the viewport. The bottom of the table
    // is where your hand and your plaque are, so a strip pinned to the viewport's
    // bottom edge lands on top of them. That is not a hypothetical: it is what
    // C2's first run drew, through the plaque.
    const QRectF sceneBand = table ? table->overlayBand() : QRectF();

    // IN TABLE SPACE, so it goes through the transform to find the pixels it
    // covers, and through the viewport's offset to find them in this widget,
    // which is the strip's parent. The offset is zero with no frame, which is
    // this view's - but a frame added one day would otherwise move every strip.
    const QRect band = sceneBand.isEmpty()
                           ? QRect()
                           : mapFromScene(sceneBand).boundingRect()
                                 .translated(viewport()->geometry().topLeft());

    // Bottom-aligned within the band, so the strip sits just above your hand and
    // grows UPWARD over the middle of the table. That is deliberate: bidding
    // finishes before the round's first card is played, so the middle is empty
    // while this is up.
    //
    // The fallback is the viewport itself, which is only reached before the
    // first snapshot - and at that point there is nothing drawn to collide with.
    const int top = band.isEmpty() ? 0 : band.top();
    const int bottom = band.isEmpty() ? viewportSize.height() : band.bottom();

    // CLAMPED TO THE BAND, not to the viewport. A strip taller than the room
    // above the plaque has to lose the difference somewhere, and clamping only
    // the y below would leave the height intact and push the bottom edge back
    // down over the plaque - the exact collision the band exists to prevent.
    const int height = std::min(preferred, bottom - top);

    bottomOverlay->setGeometry((viewportSize.width() - width) / 2,
                               std::max(top, bottom - height),
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

    // THE SCALE AND NOTHING ELSE. The scene is in table space and does not know
    // the window has changed size; nothing in it moves.
    fitTable();

    // AFTER the scale, and the order is load-bearing: the overlay is placed by
    // mapping overlayBand() through the transform just set. Placed first, it
    // would be positioned against the PREVIOUS scale on every resize - correct
    // only by one frame's delay, and visibly wrong while a window is dragged.
    placeOverlay();
}
