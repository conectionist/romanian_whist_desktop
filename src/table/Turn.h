#ifndef TURN_H
#define TURN_H

#include <QGraphicsItem>

// TURNS `item` TO `degrees`, AND ONLY WHEN IT TURNS.
//
// QGraphicsItem::setRotation() calls prepareGeometryChange() before it checks
// whether the angle has changed, so setting the angle an item already has repaints
// its whole area. The racks, the played cards and the lead rhombus are all turned
// on every snapshot - about 1300 a game - so a snapshot that changed nothing on the
// table repainted all of them, and everything beneath (PHASE5_REVIEW_FINDINGS.md #8).
// Every item turned from a snapshot is turned through this (#17).
inline void turnTo(QGraphicsItem& item, qreal degrees)
{
    if(item.rotation() != degrees)
        item.setRotation(degrees);
}

// SCALES `item` TO `factor`, AND ONLY WHEN IT CHANGES, for the same reason:
// setScale() prepares a geometry change before it compares. A played card at rest
// is set to 1 on every snapshot since a card can be left mid-flight (phase 12).
inline void scaleTo(QGraphicsItem& item, qreal factor)
{
    if(item.scale() != factor)
        item.setScale(factor);
}

#endif
