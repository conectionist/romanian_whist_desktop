#ifndef TABLE_TOP_ITEM_H
#define TABLE_TOP_ITEM_H

#include "paint/PictureMemo.h"
#include "paint/Theme.h"

#include <QGraphicsItem>
#include <QPixmap>
#include <QRectF>

#include <cstddef>

// THE TABLE ITSELF, section 2.4 of docs/ui-redesign/IMPLEMENTATION_PLAN.md: its
// shadow on the room, the wooden rim, the bead, the felt with its vignette and
// gold circle, and the logo plaque in the middle - one picture, in table space.
//
// WHERE THE MOCK-UP AND SECTION 2.4 DISAGREE, the mock-up's page.html is what is
// drawn, which the document's own rule asks for. The definitions say where.
namespace tabletop
{

// What the picture covers, in table units: the table's shadow at its full reach
// round the centre, which is everything it draws.
QRectF bounds();

// The picture, rasterized at `ratio` physical pixels per table unit and carrying
// that ratio - so it is laid out at bounds() and blitted 1:1 wherever the view's
// scale times the screen's ratio is `ratio`.
//
// PAINTED, AND SLOW BY THE STANDARDS OF A FRAME: textures scaled with a box filter,
// gradients over the whole picture, a blurred shadow. TableTopItem below holds the
// result and paints it again only when the ratio or the colours change.
QPixmap render(const Theme& theme, qreal ratio);

} // namespace tabletop

// The table in the scene: tabletop::render(), cached.
//
// AT THE BOTTOM OF THE STACK (tablespace::z::table), and the logo is in the same
// picture: section 3's ladder has nothing between the two, so one picture is one
// cache and one blit rather than two.
//
// A PLAIN QGraphicsItem, not a QGraphicsObject: nothing animates the table, and it
// takes no input.
class TableTopItem : public QGraphicsItem
{
public:
    TableTopItem();

    // The colours, and the wood and felt textures, come from here.
    //
    // ONLY THE TABLE'S OWN PALETTE, theme.table, ASKS FOR A NEW PICTURE: it is all of
    // the theme the picture reads besides the textures and the logo's font - and
    // those are fixed resources. The rest of the theme changes between light and
    // dark while the table does not, so the whole theme would paint the same
    // picture again, and repaint the felt, whenever the system's colour scheme
    // flipped (PHASE5_REVIEW_FINDINGS.md #18).
    void setTheme(const Theme& theme);

    QRectF boundingRect() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // THE PICTURE AT `ratio`, from the cache when it holds that ratio and the
    // table's colours. What paint() blits; public because it is the contract a test can
    // check without a view.
    QPixmap picture(qreal ratio) const;

    // How many times the picture has been painted. READ-ONLY and for tests, which
    // are how a resize is shown to leave the cache alone (TableViewTests).
    std::size_t renders() const { return memo.compositions(); }

private:
    Theme theme = Theme::light();

    // ONE PICTURE AT A TIME, as every item on the table keeps its own. A new ratio
    // means the window was resized, and the old one is never asked for again.
    mutable PictureMemo memo;
};

#endif
