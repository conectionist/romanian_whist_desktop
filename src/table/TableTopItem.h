#ifndef TABLE_TOP_ITEM_H
#define TABLE_TOP_ITEM_H

#include "paint/PictureMemo.h"
#include "paint/Theme.h"

#include <QGraphicsItem>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QStringList>

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

// THE LOGO'S WORDS, a line of lettering for each line of one translated phrase:
// "Romanian" over "Whist" in English, as page.html has them, and in a language that
// names the game in one word, one line - Romanian calls it plain "Whist" (the owner's
// call, 2026-10-23). One phrase rather than a string per line, because a translation
// cannot leave a line out: Qt shows the source for an empty translation.
QStringList logoLines();

// `phrase` as the logo's lines: split at its line breaks, blank lines dropped.
QStringList logoLinesFrom(const QString& phrase);

// THE LOGO'S OUTLINE, in its own frame before the 0.9 it is drawn at: page.html's
// plaque, 270 x 142 rounded 14, which the main-menu page keeps; or, on the table
// since phase 13 (the owner's call, 2026-10-23), a MEDALLION - a circle, which is
// a square box rounded by half its side, so every layer of the plaque is drawn
// the same way round.
struct LogoShape
{
    QSizeF box;
    qreal radius = 0.0;

    // .logo::before's fine gold line, rounded 9 on the plaque.
    qreal innerLineRadius = 0.0;

    // The ♣ either side of the words: the plaque's. A circle has no room beside
    // its words, which take its whole width.
    bool clubs = true;

    bool operator==(const LogoShape&) const = default;
};

LogoShape plaque();

// A circle `diameter` table units across, its border included.
LogoShape medallion(qreal diameter);

// THE LOGO ON THE TABLE: the medallion, at tablespace::logoDiameter.
LogoShape tableLogo();

// HOW MUCH THE ORNAMENTS AND LETTERING ARE SCALED to sit in `shape`: 1 on the plaque.
// In a medallion, as large as fits inside its fine gold line with room to spare,
// and never larger than on the plaque - so a short word, Romanian's "Whist", keeps
// the plaque's size where the English two lines shrink.
qreal letteringScale(const LogoShape& shape, const QStringList& lines);

// THE LOGO ALONE (.panel.logo), centred on `centre` in the painter's units: part
// of render()'s picture, and the main-menu page's title since the redesign's phase
// 10. Drawn at section 2.4's 0.9 of its frame, its drop shadow included. `lines`
// are its words, one line of lettering each.
void paintLogo(QPainter& painter, const Theme& theme, QPointF centre, const QStringList& lines = logoLines(),
               const LogoShape& shape = plaque());

// WHERE THE LETTERING GOES for `lineCount` lines, in the logo's own frame and
// before letteringScale(): as tall as its lines, its two ornaments above and below
// it, and the three centred in the logo together - so a one-line logo sits in the
// middle.
QRectF letteringRect(int lineCount, const LogoShape& shape = plaque());

// What paintLogo() covers about the centre it is given, its shadow included.
QRectF logoBounds(const LogoShape& shape = plaque());

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
