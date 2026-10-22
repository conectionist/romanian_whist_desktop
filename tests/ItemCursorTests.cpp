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

    // None: the arrow on the way out, and then no cursor of its own.
    itemcursor::show(probe, std::nullopt);
    REQUIRE_FALSE(probe.hasCursor());
    const int cleared = probe.changes;

    itemcursor::show(probe, std::nullopt);
    REQUIRE(probe.changes == cleared);
    REQUIRE_FALSE(probe.hasCursor());
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
    REQUIRE_FALSE(parent->hasCursor());
}
