#ifndef TABLE_VIEW_H
#define TABLE_VIEW_H

#include <QGraphicsView>

class TableScene;

// The window onto the table.
//
// SCENE UNITS ARE VIEW PIXELS, ONE TO ONE. No fitInView, no scale(), no
// transform of any kind - the scene rect is set to the viewport and the layout
// inside it is recomputed instead.
//
// That is not a preference. CardRenderer rasterizes each card at exactly the
// size it will be drawn at, times the device pixel ratio, which is the whole of
// what B1 built; a view transform would then resample that pixmap on its way to
// the screen and undo it at the last step. A scaled QGraphicsView is the
// idiomatic way to make a scene fit, and it is the one thing this must not do.
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
    // HERE RATHER THAN IN MainWindow BECAUSE THIS IS WHAT KNOWS THE VIEWPORT,
    // which differs from the widget by the frame - resizeEvent already
    // re-derives the scene rect from it for exactly that reason.
    //
    // NOT OWNED. Qt's parent/child owns the widget; this only places it, and
    // passing nullptr forgets it.
    void setBottomOverlay(QWidget* overlay);

protected:
    void resizeEvent(QResizeEvent* event) override;

    // Watches the overlay, whose size changes with the prompt it is showing -
    // see the definition.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Puts the overlay where setBottomOverlay() promises. Called on resize and
    // whenever the overlay changes, because a strip set on an already-sized view
    // would otherwise sit at the origin until the next resize - which on a
    // window nobody drags is forever.
    void placeOverlay();

    TableScene* table;

    // Non-owning; null until a caller sets one. See setBottomOverlay().
    QWidget* bottomOverlay = nullptr;
};

#endif
