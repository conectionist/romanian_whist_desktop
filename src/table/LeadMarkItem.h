#ifndef LEAD_MARK_ITEM_H
#define LEAD_MARK_ITEM_H

#include "paint/PictureMemo.h"
#include "paint/Theme.h"
#include "table/TrickItem.h"

#include <QGraphicsItem>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

#include <cstddef>
#include <optional>

// WHO LEADS THIS TRICK (the table redesign's section 2.7): a small gold rhombus on
// the felt's gold circle, between the leader's rack and their card - or, for you,
// between your card and your hand. It replaces the old pip under the first card,
// and says "which suit are we following" before a card is down as well as after.
//
// table.js's .leadpip: an 11 x 11 square turned 45 degrees and then stretched 1.45
// along the radius, so its long axis points at the leader. The item's rotation
// (trickitem::leadMarkPlacement()) turns its frame so +y runs out along the radius,
// and the rhombus is long in y.
class LeadMarkItem : public QGraphicsItem
{
public:
    explicit LeadMarkItem(QGraphicsItem* parent = nullptr);

    // Where the mark goes, or none, and the table's colours for it.
    void setMark(std::optional<trickitem::Placement> place, const Theme::TablePalette& palette);

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // How many times the picture has been drawn: READ-ONLY and for tests, which show
    // with it that a resize settling leaves it alone.
    std::size_t renders() const { return memo.compositions(); }

private:
    Theme::TablePalette palette;

    // ONE CACHED PICTURE, for the glow's blur: tiny, but a blur all the same, and
    // drawn on every frame of a drag otherwise. Drawn before the stretch, as CSS
    // blurs the box before its transform, at the ratio the stretch will magnify it
    // to.
    mutable PictureMemo memo;
};

#endif
