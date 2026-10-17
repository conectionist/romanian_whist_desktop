#ifndef TABLE_VIEW_H
#define TABLE_VIEW_H

#include <QGraphicsView>
#include <QTimer>

#include <optional>

class TableScene;

// The window onto the table.
//
// A SCALED VIEW OF A FIXED TABLE. The scene is laid out once in table space
// (TableSpace.h) and this fits it to the window with a uniform scale, centred -
// the table redesign's section 2.1, and its mock-up's fit(). A resize changes the
// scale and nothing else, so the table can neither jump nor reflow.
//
// IT WAS THE OPPOSITE UNTIL THE REDESIGN'S PHASE 2: scene units were view pixels,
// one to one, and the layout was recomputed per window size instead. The reason
// was B1's: CardRenderer rasterizes each card at exactly the size it is drawn at,
// times the device pixel ratio, and a view transform would resample that pixmap
// on its way to the screen and undo it at the last step. That contract still
// holds - every pixmap now asks devicepixels::effectiveRatio() for the pixels it
// lands on, the view's scale included, and while a resize settles this view
// freezes that answer on its viewport (resizeEvent()). So the scale costs the cards nothing, and
// everything drawn as vectors was never at risk.
//
// AND THE ROOM BEHIND THE TABLE, which is this view's background: it fills the
// whole viewport, the letterbox the fit box leaves included.
class TableView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit TableView(TableScene* scene, QWidget* parent = nullptr);

    // The widget floated over the BOTTOM of the viewport - section 15.1's bid
    // strip, which "rises over the bottom of the table".
    //
    // AN OVERLAY RATHER THAN A ROW IN THE WINDOW'S LAYOUT, and B2b's review is
    // what settled that: a widget whose size depends on the round moves
    // everything laid out beneath it, which cost a ~50 pixel jump twice a game
    // when the trump label was hidden in the eight-card round. A strip that
    // appears only on your turn would do the same thing every round, and
    // reserving a permanent row for a bank of buttons spends that height on the
    // table for the whole game. An overlay changes no geometry at all.
    //
    // HERE RATHER THAN IN MainWindow BECAUSE THIS IS WHAT KNOWS THE VIEWPORT AND
    // THE TRANSFORM. The floor the strip sits on is in table space, and only the
    // view can say where that lands on the screen.
    //
    // NOT OWNED. Qt's parent/child owns the widget; this only places it, and
    // passing nullptr forgets it.
    void setBottomOverlay(QWidget* overlay);

    // tablespace::minimumViewport(): the size that draws the table at its
    // minimum scale. A HINT rather than a minimum size, so the layouts carry it up
    // to the window - which then refuses a size that would draw the table too
    // small to read - while a bare view, as the tests use, can still be squeezed.
    QSize minimumSizeHint() const override;

    // ENDS A RESIZE'S SETTLING, which the settle timer does on its own 150 ms after
    // the last resize event - see resizeEvent(). Public so a test can settle the
    // view at once rather than wait for it.
    void finishResize();

    // Whether a resize is settling: the pixmaps on the table are being drawn
    // scaled from a scale the resize painted them at, and are rasterized afresh
    // once it ends. Not true of a resize's first step, which is drawn sharp.
    bool isSettling() const { return frozenScale.has_value(); }

protected:
    void resizeEvent(QResizeEvent* event) override;

    // Notes the scale a resize painted at, which is the one it may freeze - see
    // resizeEvent().
    void paintEvent(QPaintEvent* event) override;

    // The room, over the whole viewport - see the definition.
    void drawBackground(QPainter* painter, const QRectF& rect) override;

    // Watches the overlay, whose size changes with the prompt it is showing -
    // see the definition.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Scales the fit box to the viewport. See the definition.
    void fitTable();

    // Puts the overlay where setBottomOverlay() promises. Called on resize and
    // whenever the overlay changes, because a strip set on an already-sized view
    // would otherwise sit at the origin until the next resize - which on a
    // window nobody drags is forever.
    void placeOverlay();

    // The scene, as the TableScene it is - or null when it has none. ASKED OF
    // QGraphicsView each time rather than kept: Qt drops a scene from its views
    // when it is destroyed, so this cannot dangle.
    TableScene* tableScene() const;

    // Non-owning; null until a caller sets one. See setBottomOverlay().
    QWidget* bottomOverlay = nullptr;

    // THE RESIZE SETTLING. See resizeEvent().
    //
    // `fittedScale` is the scale of the last fit, zero before the first. The timer
    // runs from a resize until the window has stopped changing size for 150 ms -
    // a burst. `paintedInBurst` is a scale the viewport was painted at, unfrozen,
    // during the burst; `frozenScale`, the one the pixmaps are frozen at, once the
    // burst has gone on long enough to be a drag.
    qreal fittedScale = 0.0;
    QTimer settleTimer;
    std::optional<qreal> paintedInBurst;
    std::optional<qreal> frozenScale;
};

#endif
