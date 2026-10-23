#include <catch2/catch_test_macros.hpp>

#include "RestingMouse.h"

#include "table/ItemCursor.h"

#include <QCursor>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QVariant>

#include <optional>

// itemcursor::show() (PR #38's review, findings #4 and #6): the view shows the
// cursor at once, and nothing happens when nothing changes.
namespace
{

// AN ITEM THAT COUNTS ITS CURSOR CHANGES. Every setCursor() passes through
// itemChange(), and each one has Qt look for the items under the mouse again.
struct Probe : QGraphicsRectItem
{
    int changes = 0;

    Probe() : QGraphicsRectItem(0.0, 0.0, 100.0, 100.0) {}

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if(change == ItemCursorChange)
            changes++;

        return QGraphicsRectItem::itemChange(change, value);
    }
};

}

TEST_CASE("Asking for the cursor already shown changes nothing", "[cursor]")
{
    Probe probe;

    itemcursor::show(probe, Qt::PointingHandCursor);
    itemcursor::show(probe, Qt::PointingHandCursor);
    REQUIRE(probe.changes == 1);
    REQUIRE(probe.cursor().shape() == Qt::PointingHandCursor);

    itemcursor::show(probe, Qt::ForbiddenCursor);
    REQUIRE(probe.changes == 2);
    REQUIRE(probe.cursor().shape() == Qt::ForbiddenCursor);

    // None: the arrow, set as any other cursor is - never unset (see the header).
    itemcursor::show(probe, std::nullopt);
    REQUIRE(probe.changes == 3);
    REQUIRE(probe.cursor().shape() == Qt::ArrowCursor);

    itemcursor::show(probe, std::nullopt);
    REQUIRE(probe.changes == 3);

    // And an item that never asked for a cursor is left alone: the arrow is what it shows.
    Probe untouched;
    itemcursor::show(untouched, std::nullopt);
    REQUIRE(untouched.changes == 0);
    REQUIRE(untouched.cursor().shape() == Qt::ArrowCursor);
}

// THE VIEW, not only the item: what QGraphicsItem::unsetCursor() alone gets wrong
// for an item with a child over it.
TEST_CASE("No cursor puts the arrow back at once, even under a child", "[cursor]")
{
    QGraphicsScene scene;

    Probe* parent = new Probe;
    scene.addItem(parent);

    // Over the whole of its parent, as a fan's cards are over the fan.
    new QGraphicsRectItem(0.0, 0.0, 100.0, 100.0, parent);

    RestingMouse mouse(scene, QPointF(50.0, 50.0));

    itemcursor::show(*parent, Qt::PointingHandCursor);
    REQUIRE(mouse.shown() == Qt::PointingHandCursor);

    itemcursor::show(*parent, std::nullopt);
    REQUIRE(mouse.shown() == Qt::ArrowCursor);
    REQUIRE(parent->cursor().shape() == Qt::ArrowCursor);
}

// A VIEW UNDER A MOUSE THAT HAS NOT MOVED SINCE IT APPEARED has had no mouse event, and
// QGraphicsItem::unsetCursor() on the item topmost there has the view look under that
// missing event: a crash inside Qt. The table page comes up so after New Game's dialog
// (found after PR #38's review: the first fix for finding #4 unset the strip's cursor
// as you bid). "None" must not go that way.
TEST_CASE("No cursor is safe under a mouse that has not moved", "[cursor]")
{
    QGraphicsScene scene;

    Probe* item = new Probe;
    scene.addItem(item);

    RestingMouse mouse(scene, QPointF(50.0, 50.0), RestingMouse::Arrival::Appeared);

    itemcursor::show(*item, Qt::PointingHandCursor);
    REQUIRE(mouse.shown() == Qt::PointingHandCursor);

    itemcursor::show(*item, std::nullopt);
    REQUIRE(mouse.shown() == Qt::ArrowCursor);
}
