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

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    TableScene* table;
};

#endif
