#ifndef RESTING_MOUSE_H
#define RESTING_MOUSE_H

#include <QCoreApplication>
#include <QCursor>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>

// A VIEW WITH THE MOUSE RESTING ON IT, over scene point `at`, as far as Qt can tell:
// the view is under the mouse and the cursor is there. shown() is the cursor the
// view shows - the one a player sees - where an item's own cursor() is only what
// the item asked for. The two part ways under a mouse that does not move (PR #38's
// review, finding #4).
//
// NO FRAME, so the view's coordinates and its viewport's are the same: Qt's
// QGraphicsItem::setCursor() and unsetCursor() read the cursor in one and look for
// items in the other.
//
// AND THE MOUSE MOVED THERE, through the view, as a player's does: the scene's items
// hear it as a hover. It is also what a view needs before an item may unset its
// cursor - QGraphicsView looks again under its last mouse event, which it does not
// have until the first, and dereferences it all the same.
struct RestingMouse
{
    QGraphicsView view;

    RestingMouse(QGraphicsScene& scene, QPointF at) : view(&scene)
    {
        view.setFrameShape(QFrame::NoFrame);
        view.resize(1200, 800);
        view.centerOn(at);
        view.show();

        const QPoint local = view.mapFromScene(at);
        const QPoint global = view.viewport()->mapToGlobal(local);

        QCursor::setPos(global);

        // Set here rather than left to an enter event the test does not wait for.
        view.setAttribute(Qt::WA_UnderMouse, true);

        QMouseEvent move(QEvent::MouseMove, QPointF(local), QPointF(global), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &move);
    }

    Qt::CursorShape shown() const { return view.viewport()->cursor().shape(); }
};

#endif
