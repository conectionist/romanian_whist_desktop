#include "TableView.h"

#include "paint/DevicePixels.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"

#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QResizeEvent>
#include <QTransform>

#include <optional>

namespace
{

// HOW LONG A RESIZE HAS TO STOP FOR before the table is drawn sharp again. Long
// enough that a hand dragging the window's edge does not trip it between two
// steps of the drag, short enough that the moment the hand stops the cards are
// crisp before anyone looks for them.
constexpr int settleDelayMs = 150;

}

TableView::TableView(TableScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    // Nothing scrolls: the scaled table always fits inside the viewport. Scrollbars
    // would also change the viewport size when they appeared, which is a resize
    // that rescales the table and can bring them back.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);

    // The room comes from Theme through drawBackground() below, which covers the
    // whole viewport, so the viewport must not paint its own beneath it.
    setBackgroundBrush(Qt::NoBrush);
    viewport()->setAutoFillBackground(false);

    // A card moving in the middle of the table repaints the table. The default
    // MinimalViewportUpdate leaves seams behind an item whose bounding rect
    // changes, and the table is small enough that the whole viewport is cheap.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // PLACED BY THE TRANSFORM, not by the alignment or a scroll. fitTable() puts
    // the table exactly where fit() does and makes this view's scene rect exactly
    // the viewport, so QGraphicsView has nothing to align and nothing to scroll -
    // and a rect that IS the viewport, aligned to its top-left, does not move.
    //
    // QGraphicsView's own centring could not do this. It halves the viewport in
    // whole pixels and truncates its scroll offset, which put the table up to a
    // pixel off on either axis - and it centres in the whole viewport, where fit()
    // centres in what the 10-and-8 margins leave. The anchors are moot, and
    // NoAnchor says so rather than leaving a default to wonder about.
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);

    // Restarted by every resize event, so it fires once the window has stopped
    // changing size - see resizeEvent().
    settleTimer.setSingleShot(true);
    settleTimer.setInterval(settleDelayMs);

    connect(&settleTimer, &QTimer::timeout, this, &TableView::finishResize);
}

// The view has no frame and no scrollbars, so its own size is the viewport's.
QSize TableView::minimumSizeHint() const
{
    return tablespace::minimumViewport();
}

// table.js fit(): one scale for both axes, so the table stays round; the largest
// that fits the fit box inside the viewport's margins; and the box centred in
// what the margins leave, which with 10 above and 8 below is a pixel under the
// viewport's own middle.
//
// SET BY HAND RATHER THAN THROUGH fitInView(), which would get the same scale
// less a 2-pixel margin of its own that nothing in the mock-up has.
void TableView::fitTable()
{
    const QSizeF size = viewport()->size();
    const qreal scale = tablespace::fitScale(size);

    // A viewport with no room inside its margins - a window being created, or one
    // squashed to nothing. The last transform stays rather than a zero one that
    // would make it singular.
    if(scale <= 0.0)
        return;

    fittedScale = scale;

    const QSizeF padded(size.width() - 2.0 * tablespace::marginX,
                        size.height() - tablespace::marginTop - tablespace::marginBottom);

    // Where the box's top-left corner lands, in viewport pixels - fractional, and
    // kept that way.
    const QPointF corner(
        tablespace::marginX + (padded.width() - tablespace::fitBox.width() * scale) / 2.0,
        tablespace::marginTop + (padded.height() - tablespace::fitBox.height() * scale) / 2.0);

    QTransform transform = QTransform::fromTranslate(corner.x(), corner.y());
    transform.scale(scale, scale);
    transform.translate(-tablespace::fitBox.left(), -tablespace::fitBox.top());

    setTransform(transform);

    // THIS VIEW'S scene rect, not the scene's, which stays the fit box: the
    // viewport itself, in table space. See the constructor for why that leaves
    // the translation above as the whole of where the table is drawn.
    const QRectF visible = transform.inverted().mapRect(QRectF(QPointF(0.0, 0.0), size));

    setSceneRect(visible);

    // AND THE CORNER PANELS PINNED TO IT: the only items a resize moves, because
    // they belong to the window's corners rather than to the table (section 2.1).
    if(TableScene* table = tableScene())
        table->pinHud(visible, scale);
}

// THE ROOM, over the whole viewport. The fit box keeps the table's shape, so a
// window of any other shape shows bands beside or above it, and the mock-up's
// room is the viewport's background rather than the stage's - so it fills those
// too. Through Theme, which is what keeps it replaceable (section 18.1).
//
// HERE RATHER THAN IN THE SCENE because the room is the viewport's, and only the
// view knows its viewport. The scene had to work it out from whatever device it
// was handed, by device type, and took the whole device for the viewport - which
// is wrong for anything drawn into part of an image.
void TableView::drawBackground(QPainter* painter, const QRectF& rect)
{
    const TableScene* table = tableScene();

    if(!table)
        return;

    const QSize view = viewport()->size();

    painter->save();

    // IN VIEWPORT PIXELS, not table space. The painter arrives in table space,
    // through this view's transform and whatever the caller put in front of it -
    // nothing when painting the screen, the target's mapping under render() - so
    // taking this view's transform back out leaves exactly the map from viewport
    // pixels to the device.
    painter->setWorldTransform(viewportTransform().inverted() * painter->worldTransform());

    // Only the exposed part, which is what `rect` is for: a card moving in the
    // middle of the table must not repaint the whole room behind it. Clipped to
    // the room itself, so the source rect below never reaches outside the pixmap.
    const QRectF whole(QPointF(0.0, 0.0), QSizeF(view));
    const QRectF exposed = viewportTransform().mapRect(rect).intersected(whole);

    // WHILE A RESIZE SETTLES, STRAIGHT FROM THE GRADIENT. The size changes on every
    // step of a drag, so a cached picture of the room would be painted, blitted
    // once and thrown away each frame - and stretching the last one instead costs
    // more than either, smooth scaling a picture the size of the window. Measured
    // on the release build, a full table at 1600 x 900, the median frame of a
    // drag: 10.6 ms painting a new room each step, 8.9 stretching the last one,
    // 7.4 filling from the gradient. The fill is exact, too, where a stretch is
    // not.
    if(frozenScale)
    {
        if(!exposed.isEmpty())
            painter->fillRect(exposed, table->currentTheme().roomBrush(whole));

        painter->restore();
        return;
    }

    // Otherwise the cached room, which a frame that only moved a card blits for
    // less than the fill costs. At the pixels it will cover, by the rule every
    // pixmap in the table follows: the device's ratio, times any scale render()
    // put between the viewport and the device.
    const QPixmap room = table->currentTheme().room(view, devicepixels::effectiveRatio(*painter));

    if(!room.isNull() && !exposed.isEmpty())
    {
        // THE TARGET RECT IS LOGICAL AND THE SOURCE RECT IS NOT. Qt reads a
        // drawPixmap source rect in the PIXMAP's own pixels, and room() above
        // rasterized this one at view * ratio - so passing the same logical rect
        // as both samples the top-left 1/ratio of the room and stretches it over
        // the whole viewport, with each partial repaint reading from a different
        // wrong place besides.
        //
        // QRectF throughout - rounding here would put a seam in the room at the
        // fractional ratios (1.25, 1.5) that most Linux and Windows screens use.
        const qreal ratio = room.devicePixelRatio();

        painter->drawPixmap(exposed, room,
                            QRectF(exposed.topLeft() * ratio, exposed.size() * ratio));
    }

    painter->restore();
}

void TableView::finishResize()
{
    settleTimer.stop();
    paintedInBurst.reset();

    if(!frozenScale)
        return;

    frozenScale.reset();
    devicepixels::setFreeze(*viewport(), std::nullopt);

    // Everything on the table is repainted at the scale the window stopped at. The
    // one repaint the settling asks for: freezing asked for none, because it only
    // ever happens inside a resize, which repaints the viewport anyway.
    viewport()->update();
}

TableScene* TableView::tableScene() const
{
    return qobject_cast<TableScene*>(scene());
}

void TableView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    // THE SCALE, AND THE CORNER PANELS WITH IT. The scene is in table space, so
    // the table and everything on it stay where they are; fitTable() pins the four
    // corner panels to the window's new corners, the only items a resize moves.
    fitTable();

    // AND THE PIXMAPS WAIT FOR THE WINDOW TO STOP. Every pixmap on the table is
    // rasterized at the screen's ratio times the scale - and the room behind it at
    // the viewport's size - so each step of a drag would miss every cache: every
    // card painted again from its SVG, the table composed again, a new room, for a
    // picture shown for one frame. Instead, once a resize is shown to be a drag,
    // its scale is frozen: devicepixels::effectiveRatio() answers for the viewport
    // with a scale the drag has already painted at, the pixmaps already made are
    // drawn scaled, a little soft, and the room is filled straight from its
    // gradient (drawBackground()). The timer ends it 150 ms after the last step,
    // when everything is rasterized once at the scale the window stopped at.
    //
    // ON A CHANGE OF SIZE, not of scale. A wide window is fitted by its height, so
    // dragging its side edge leaves the scale where it was - and still asks for a
    // room the new size on every step. Frozen at the same scale, the cards are
    // drawn exactly as they were and only the room is spared.
    //
    // NOT AT A RESIZE'S FIRST STEP, which is drawn sharp at full cost. Maximizing,
    // restoring, a dock shown or hidden: each is one resize, and frozen it would be
    // drawn soft for 150 ms and then rasterized again anyway. Only a second resize
    // while the timer is still running freezes - a drag, which pays for one sharp
    // frame where it began.
    //
    // AND ONLY AT A SCALE THE BURST HAS PAINTED AT (paintEvent()), which is the one
    // whose pixmaps are in their caches. Two resizes with no frame between them -
    // a window manager's double configure, a relayout before the first paint -
    // would otherwise freeze a scale nothing was drawn at: soft for no reason, and
    // everything rasterized twice. Not a hidden view's, which paints nothing; and
    // not the first fit, which has no old size.
    //
    // A FACTOR ON THE VIEWPORT, the frozen scale over the live one, set again at
    // every step because the live one moves. Not the ratio rounded to a coarser key
    // instead - that brings back the soft, misplaced cards devicepixels::ratioKey()
    // was made fine to fix.
    if(isVisible() && event->oldSize().isValid() && event->oldSize() != event->size())
    {
        if(!frozenScale && paintedInBurst)
            frozenScale = paintedInBurst;

        if(frozenScale)
            devicepixels::setFreeze(*viewport(), *frozenScale / fittedScale);

        settleTimer.start();
    }
}

void TableView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);

    // A frame of a burst drawn sharp, so its pixmaps are now cached at this scale:
    // the one the rest of the burst may freeze at. Never a frozen frame's, whose
    // pixmaps are still the earlier scale's.
    if(settleTimer.isActive() && !frozenScale)
        paintedInBurst = fittedScale;
}
