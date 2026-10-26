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
#include <QWidget>
#include <QWindow>

// THE MOUSE, PUT BACK WHERE IT WAS. There is one mouse for the whole test binary, and
// these helpers move it. A mouse left resting mid-screen is over whatever window a
// later test opens there - with no mouse event to tell that window so, which is the
// state itemcursor::show() describes, and which crashed a window test that ran after
// one of these (PR #38's review round). So each move is undone as its owner goes:
// to where the mouse was before, which is the screen's origin, where the offscreen
// platform starts it and every other test was written against.
class CursorReturn
{
public:
    // Notes where the mouse is, to put it back there.
    CursorReturn() : home(QCursor::pos()) {}

    // And moves it to `to` through the window system, as a player's mouse moves: the
    // offscreen platform's QCursor::setPos() sends the window under it an enter and a
    // move. Taken at once, so none of them reaches a window a later test has there.
    explicit CursorReturn(QPoint to) : CursorReturn()
    {
        QCursor::setPos(to);
        QCoreApplication::processEvents();
    }

    ~CursorReturn()
    {
        QCursor::setPos(home);
        QCoreApplication::processEvents();
    }

    CursorReturn(const CursorReturn&) = delete;
    CursorReturn& operator=(const CursorReturn&) = delete;

private:
    QPoint home;
};

// A VIEW WITH THE MOUSE RESTING ON IT, over scene point `at`, as far as Qt can tell:
// the view is under the mouse and the cursor is there. shown() is the cursor the
// view shows - the one a player sees - where an item's own cursor() is only what
// the item asked for. The two part ways under a mouse that does not move (PR #38's
// review, finding #4).
//
// NO FRAME, so the view's coordinates and its viewport's are the same: Qt's
// QGraphicsItem::setCursor() reads the cursor in one and looks for items in the other.
//
// HOW THE MOUSE GOT THERE:
//
// - Moved: through the view, as a player's does, so the scene's items hear it as a
//   hover and the view has a mouse event of its own.
// - Appeared: the view came up under a mouse that has not moved since, as the table
//   page does after New Game's dialog. It is under the mouse and has had no mouse
//   event; QGraphicsItem::unsetCursor() on an item topmost there that has a cursor
//   then crashes inside Qt (see itemcursor::show()).
struct RestingMouse
{
    enum class Arrival
    {
        Moved,
        Appeared,
    };

    // FIRST, so it is destroyed last: the mouse goes home once the view has gone.
    CursorReturn back;

    QGraphicsView view;

    RestingMouse(QGraphicsScene& scene, QPointF at, Arrival arrival = Arrival::Moved) : view(&scene)
    {
        view.setFrameShape(QFrame::NoFrame);
        view.resize(1200, 800);
        view.centerOn(at);
        view.show();

        const QPoint local = view.mapFromScene(at);
        const QPoint global = view.viewport()->mapToGlobal(local);

        if(arrival == Arrival::Appeared)
        {
            // THE MOUSE FIRST, WITH NO WINDOW THERE TO HEAR IT, and then the view back over
            // it, in the same place: nothing has told the view of the mouse.
            view.hide();
            QCoreApplication::processEvents();

            QCursor::setPos(global);
            QCoreApplication::processEvents();

            view.show();
            QCoreApplication::processEvents();

            // What the window system's enter would have set as the view came up under it.
            view.setAttribute(Qt::WA_UnderMouse, true);
            return;
        }

        // Taken now: the offscreen platform queues an enter and a move for it, which
        // left for later would reach whatever window a later test has there.
        QCursor::setPos(global);
        QCoreApplication::processEvents();

        // Set here rather than left to that enter, which needs the view exposed.
        view.setAttribute(Qt::WA_UnderMouse, true);

        QMouseEvent move(QEvent::MouseMove, QPointF(local), QPointF(global), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &move);
    }

    Qt::CursorShape shown() const { return view.viewport()->cursor().shape(); }
};

// AND FOR A WIDGET: the mouse moved onto the middle of `widget`, through the window
// system as a player's is, so QApplication picks the window's cursor from the widget
// under it. The widget's window must be shown. Keep what this returns for as long as
// the mouse should rest there: it puts the mouse back as it goes.
[[nodiscard]] inline CursorReturn restOn(QWidget& widget)
{
    return CursorReturn(widget.mapToGlobal(widget.rect().center()));
}

// The cursor the window `widget` is in shows: the one a player sees, where the
// widget's own cursor() is only what it asked for.
inline Qt::CursorShape shownOver(const QWidget& widget)
{
    return widget.window()->windowHandle()->cursor().shape();
}

#endif
