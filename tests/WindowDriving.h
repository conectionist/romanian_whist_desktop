#ifndef WINDOW_DRIVING_H
#define WINDOW_DRIVING_H

#include "table/BidStripItem.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

// What every test that drives the real window needs, in one place.
//
// MainWindowTests.cpp carried these privately until tests/ScreenshotTool.cpp
// wanted the same loops, and two copies of an event-loop helper are two places a
// fix has to reach. GameFixtures.h made the same move for the engine-side
// helpers, for the same reason.

namespace driving
{

// Runs the event loop for a while. Not a sleep: the queued snapshots this is
// waiting for are delivered by the loop, so a blocking wait would hold the very
// thread that has to deliver them - the asymmetry HumanSeatTests records as
// "anything answering a prompt has to pump, not wait".
//
// AND NOT A SPIN. processEvents() with a time limit returns at once when the
// queue is empty, so a loop of those kept a core busy for the whole wait - on the
// machine whose game thread it was waiting for. WaitForMoreEvents blocks instead,
// and the tick is what bounds the block: with nothing else queued, an idle loop
// would otherwise sleep straight past the deadline.
inline void pump(int ms)
{
    QTimer tick;
    tick.start(20);

    QElapsedTimer clock;
    clock.start();

    while(clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
}

// Pumps until `ready` answers true, or until the deadline. Returns what it last
// saw, so the caller asserts on a bool rather than on a hang - a game that never
// ends must fail a test rather than stop it. Blocks between events as pump() does.
inline bool pumpUntil(int limitMs, const std::function<bool()>& ready)
{
    QTimer tick;
    tick.start(20);

    QElapsedTimer clock;
    clock.start();

    while(clock.elapsed() < limitMs)
    {
        if(ready())
            return true;

        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
    }

    return ready();
}

// CLICKS A BID ON THE STRIP AS A PLAYER DOES: a press and a release through the
// scene, at the middle of the bid's button, so the scene decides which item takes
// them. Found by the bid it offers rather than by position. False when the strip
// is not up or has no button for `bet`; a refused bid's click is made, and the
// strip refuses it.
inline bool clickBid(BidStripItem& strip, unsigned int bet)
{
    QGraphicsScene* scene = strip.scene();
    const QRectF button = strip.buttonRect(bet);

    if(!scene || !strip.isVisible() || button.isEmpty())
        return false;

    const QPointF at = strip.mapToScene(button.center());

    QGraphicsSceneMouseEvent down(QEvent::GraphicsSceneMousePress);
    down.setScenePos(at);
    down.setButton(Qt::LeftButton);
    down.setButtons(Qt::LeftButton);
    QCoreApplication::sendEvent(scene, &down);

    QGraphicsSceneMouseEvent up(QEvent::GraphicsSceneMouseRelease);
    up.setScenePos(at);
    up.setButton(Qt::LeftButton);
    QCoreApplication::sendEvent(scene, &up);

    return true;
}

// What answerModalStrictly() saw: whether the dialog it was waiting for came, and
// everything that stopped it answering the way it was asked to. Empty problems
// and `found` together are the only success.
struct DialogAnswer
{
    bool found = false;
    QStringList problems;
};

// A control found BY NAME, or a problem naming the one that was not there.
//
// THE DIFFERENCE FROM A BARE findChild() IS THE POINT. A lookup that comes back
// null and is skipped leaves that setting at whatever the dialog opened with, and
// nothing fails - a renamed control turns into a different game rather than an
// error. The caller still null-checks; it just cannot do so quietly.
template <class Control>
Control* need(QObject& parent, const QString& name, DialogAnswer& answer)
{
    Control* control = parent.findChild<Control*>(name);

    if(!control)
        answer.problems << QStringLiteral("no control named %1").arg(name);

    return control;
}

// Answers a modal dialog FROM INSIDE ITS NESTED EVENT LOOP, as MainWindowTests'
// answerModal* helpers do, and fails where they would skip or hang.
//
// IT KEEPS LOOKING. A single-shot timer that finds no dialog returns; if the
// dialog then opens late, exec() waits for an answer nobody is going to give.
// This polls until a `Dialog` is the active modal widget, so one that opens late,
// or under another modal, is still answered.
//
// AND IT GIVES UP LOUDLY. Past the deadline it closes every modal still up -
// which is what lets the caller's click() return - and says so in `answer`, so a
// dialog that never came is a failure with a message rather than a run that never
// ends.
//
// `fill` sets the dialog up and answers it, accepted or rejected, itself; it
// records whatever stopped it in `answer.problems`. The caller owns `poll` and
// `answer`, both of which must outlive the click() that opens the dialog, and
// stops `poll` and checks `answer` once that click() returns.
template <class Dialog>
void answerModalStrictly(QTimer& poll, DialogAnswer& answer,
                         std::function<void(Dialog&, DialogAnswer&)> fill,
                         int deadlineMs = 10000)
{
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    poll.setInterval(50);

    QObject::connect(&poll, &QTimer::timeout, &poll,
                     [&poll, &answer, fill = std::move(fill), clock, deadlineMs]
    {
        if(Dialog* dialog = qobject_cast<Dialog*>(QApplication::activeModalWidget()))
        {
            poll.stop();
            answer.found = true;
            fill(*dialog, answer);
            return;
        }

        if(clock->elapsed() < deadlineMs)
            return;

        poll.stop();
        answer.problems << QStringLiteral("no %1 appeared within %2 ms")
                               .arg(QString::fromUtf8(Dialog::staticMetaObject.className()))
                               .arg(deadlineMs);

        // BOUNDED, so a modal that refuses to close turns a hang into a failure
        // rather than into a spin.
        for(int i = 0 ; i < 8 ; i++)
        {
            QWidget* modal = QApplication::activeModalWidget();

            if(!modal)
                break;

            modal->close();
        }
    });

    poll.start();
}

} // namespace driving

#endif
