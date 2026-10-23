#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "table/TableSpace.h"
#include "table/TableTopItem.h"
#include "paint/Theme.h"

#include <QColor>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QStringList>

#include <cmath>
#include <cstdlib>

// THE TABLE ITSELF (the redesign's phase 3): the picture TableTopItem paints, held
// to its contract - the pixels it is made at, the cache it is kept in - and to
// section 2.4's geometry, read back off the picture at points where only one layer
// can show.

namespace
{

// A ratio of 2, so even the bead's 2-unit rings are 4 pixels across and a sample
// in the middle of one is clear of its anti-aliased edges.
constexpr qreal ratio = 2.0;

// The picture's pixel under a point in table units.
QColor at(const QImage& picture, QPointF point)
{
    const QPointF pixel = (point - tabletop::bounds().topLeft()) * ratio;
    return picture.pixelColor(qFloor(pixel.x()), qFloor(pixel.y()));
}

// Straight up from the table's centre, which no texture seam, logo or ornament is
// on - and the rim's colours are the same all the way round.
QPointF up(qreal radius)
{
    return tablespace::centre - QPointF(0.0, radius);
}

bool same(const QColor& got, const QColor& want)
{
    return std::abs(got.red() - want.red()) <= 1 && std::abs(got.green() - want.green()) <= 1
        && std::abs(got.blue() - want.blue()) <= 1 && std::abs(got.alpha() - want.alpha()) <= 1;
}

// Rendered once for every geometry case: at a ratio of 2 the picture is 2460
// pixels square, and painting it is the slow part.
const QImage& picture()
{
    static const QImage image =
        tabletop::render(Theme::dark(), ratio).toImage().convertToFormat(QImage::Format_ARGB32);
    return image;
}

}

TEST_CASE("The table's picture is rasterized at the ratio it is asked for", "[table-top]")
{
    const QPixmap half = tabletop::render(Theme::dark(), 0.5);

    // Whole pixels, rounded up from the box at that ratio, and carrying the ratio -
    // so it is laid out at the box and blitted 1:1.
    REQUIRE(half.width() == qCeil(tabletop::bounds().width() * 0.5));
    REQUIRE(half.height() == qCeil(tabletop::bounds().height() * 0.5));
    REQUIRE(half.devicePixelRatio() == Catch::Approx(0.5));
    REQUIRE(half.deviceIndependentSize().width()
            == Catch::Approx(tabletop::bounds().width()).margin(2.0));

    // The box reaches as far as the table's shadow does, and no further.
    REQUIRE(tabletop::bounds().width() / 2.0 == Catch::Approx(tablespace::tableReach));
    REQUIRE(tabletop::bounds().center() == tablespace::centre);
}

// ONE PICTURE AT A TIME, painted again only when it has to be: a ratio that is a
// different key, or a colour that changed. Everything else about a repaint is a
// blit, which is what makes the table cheap to draw every frame.
TEST_CASE("The table item keeps its picture until the ratio or the colours change", "[table-top]")
{
    TableTopItem item;
    item.setTheme(Theme::dark());

    const QPixmap first = item.picture(0.5);
    REQUIRE(item.renders() == 1);

    REQUIRE(item.picture(0.5).cacheKey() == first.cacheKey());
    REQUIRE(item.renders() == 1);

    // A ten-thousandth apart is another key - devicepixels::ratioKey()'s grain.
    REQUIRE(item.picture(0.5001).cacheKey() != first.cacheKey());
    REQUIRE(item.renders() == 2);

    // The last member of the palette, as ThemeTests' recolour case uses: a
    // comparison that stopped short would miss it first.
    Theme recoloured = Theme::dark();
    recoloured.table.logoOrnament = QColor(1, 2, 3);
    item.setTheme(recoloured);

    item.picture(0.5001);
    REQUIRE(item.renders() == 3);
}

// THE TABLE IS THE SAME IN LIGHT AND DARK - ThemeTests holds the palette to that -
// so the system's colour scheme flipping, which changes the rest of the theme,
// must not paint it again.
TEST_CASE("Switching between light and dark leaves the table's picture alone", "[table-top]")
{
    TableTopItem item;
    item.setTheme(Theme::light());

    const QPixmap first = item.picture(0.5);
    REQUIRE(item.renders() == 1);

    item.setTheme(Theme::dark());

    REQUIRE(item.picture(0.5).cacheKey() == first.cacheKey());
    REQUIRE(item.renders() == 1);
}

// PHASE5_REVIEW_FINDINGS.md #18: the picture was kept, but setTheme() still asked
// for the whole felt to be repainted on every flip.
TEST_CASE("Switching between light and dark repaints nothing", "[table-top]")
{
    // Before the scene, so it outlives the connection that writes to it.
    QList<QRectF> repainted;

    QGraphicsScene scene;
    auto* item = new TableTopItem;
    scene.addItem(item);
    item->setTheme(Theme::light());
    QCoreApplication::processEvents();

    QObject::connect(&scene, &QGraphicsScene::changed,
                     [&repainted](const QList<QRectF>& regions) { repainted += regions; });

    item->setTheme(Theme::dark());
    QCoreApplication::processEvents();

    REQUIRE(repainted.isEmpty());

    // The control: a palette that does change is repainted.
    Theme recoloured = Theme::dark();
    recoloured.table.logoOrnament = QColor(1, 2, 3);
    item->setTheme(recoloured);
    QCoreApplication::processEvents();

    REQUIRE_FALSE(repainted.isEmpty());
}

// SECTION 2.4'S BEAD: three rings between the felt and the wood, 440 to 449 the
// inner brown, to 451 the lighter, to 453 the dark - each opaque, so each is
// exactly its colour in its middle.
TEST_CASE("The bead is three rings of its three colours", "[table-top]")
{
    const Theme::TablePalette table = Theme::dark().table;

    REQUIRE(same(at(picture(), up(444.5)), table.beadInner));
    REQUIRE(same(at(picture(), up(450.0)), table.beadMiddle));
    REQUIRE(same(at(picture(), up(452.0)), table.woodDark));
}

// The faint gold circle on the felt, its stroke centred at 415.25: redder than the
// felt either side of it, which is all a line of a fifth of gold over green can
// be told apart by.
TEST_CASE("The gold circle is on the felt at its radius", "[table-top]")
{
    const int line = at(picture(), up(415.25)).red();

    REQUIRE(line > at(picture(), up(410.0)).red() + 10);
    REQUIRE(line > at(picture(), up(420.0)).red() + 10);
}

// The table's shadow on the room: darkest by the rim, fading outward, and gone at
// the edge of the picture - three sigmas of a blur of 70.
TEST_CASE("The table's shadow fades outward from the rim", "[table-top]")
{
    const int nearRim = at(picture(), up(515.0)).alpha();
    const int further = at(picture(), up(560.0)).alpha();
    const int furthest = at(picture(), up(600.0)).alpha();

    REQUIRE(nearRim > further);
    REQUIRE(further > furthest);

    // Half of 0.75 on the rim, near enough five units out.
    REQUIRE(nearRim == Catch::Approx(0.75 * 255 * 0.5 * std::erfc(5.0 / (35.0 * std::sqrt(2.0))))
                           .margin(4.0));

    REQUIRE(at(picture(), up(tablespace::tableReach - 1.0)).alpha() <= 1);
}

// The logo plaque, centred on the table at 0.9 of its 270 x 142: its gold border
// is at its top edge, and the felt is just above it.
TEST_CASE("The logo plaque sits on the table's centre with its gold border", "[table-top]")
{
    const Theme::TablePalette table = Theme::dark().table;

    const qreal halfHeight = tablespace::logoBox.height() * tablespace::logoScale / 2.0;
    const qreal border = 2.0 * tablespace::logoScale;

    // In the middle of the 2-unit border, drawn at 1.8.
    REQUIRE(same(at(picture(), up(halfHeight - border / 2.0)), table.logoBorder));

    // And above the plaque, the felt: green, not the border's gold.
    const QColor felt = at(picture(), up(halfHeight + 6.0));
    REQUIRE(felt.green() > felt.red());
}

// THE LOGO'S WORDS (the owner's call, 2026-10-23): one translated phrase, a line of
// lettering for each of its lines. English keeps page.html's two; Romanian names the
// game in one word, "Whist", which a string per line could not do - Qt shows the
// source for an empty translation.
TEST_CASE("The logo is a line of lettering per line of its phrase", "[table-top]")
{
    // Untranslated, the English the mock-up has.
    REQUIRE(tabletop::logoLines() == QStringList{ QStringLiteral("Romanian"), QStringLiteral("Whist") });

    REQUIRE(tabletop::logoLinesFrom(QStringLiteral("Whist")) == QStringList{ QStringLiteral("Whist") });
    REQUIRE(tabletop::logoLinesFrom(QStringLiteral("Whist\n")) == QStringList{ QStringLiteral("Whist") });
    REQUIRE(tabletop::logoLinesFrom(QStringLiteral(" Romanian \n\nWhist"))
            == QStringList{ QStringLiteral("Romanian"), QStringLiteral("Whist") });
}

// One line sits in the middle of the plaque, its ornaments close about it; two lines
// are where they always were, the 98-unit stack centred.
TEST_CASE("A one-line logo sits in the middle of its plaque", "[table-top]")
{
    const qreal middle = tablespace::logoBox.height() / 2.0;

    const QRectF one = tabletop::letteringRect(1);
    const QRectF two = tabletop::letteringRect(2);

    REQUIRE(one.center().y() == Catch::Approx(middle));
    REQUIRE(two.center().y() == Catch::Approx(middle));
    REQUIRE(two.height() == Catch::Approx(2.0 * one.height()));

    // The ornament above, the lettering, the ornament below: 12 + 74 + 12.
    REQUIRE(two.height() + 2.0 * 12.0 == Catch::Approx(98.0));

    // And it draws: a one-line plaque paints its lettering's gold across its middle row.
    QImage image(QSize(400, 260), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        tabletop::paintLogo(painter, Theme::dark(), QPointF(200.0, 130.0), { QStringLiteral("Whist") });
    }

    int gold = 0;

    for(int x = 150 ; x < 250 ; x++)
    {
        const QColor pixel = image.pixelColor(x, 130);
        if(pixel.red() > 170 && pixel.green() > 120 && pixel.blue() < 120)
            gold++;
    }

    REQUIRE(gold > 5);
}
