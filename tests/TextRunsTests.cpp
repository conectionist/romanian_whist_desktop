#include <catch2/catch_test_macros.hpp>

#include "paint/Fonts.h"
#include "paint/Icons.h"
#include "paint/TextRuns.h"
#include "paint/Theme.h"

#include <QColor>
#include <QImage>
#include <QPainter>

#include <cmath>

// A LINE IN MORE THAN ONE VOICE (paint/TextRuns.h): what the corner panels set
// their text with. What has a right answer is the line's width, where it breaks,
// and which colour a suit glyph takes - the shapes of the letters are checked by
// eye against the mock-up.
namespace
{

using textruns::Emphasis;
using textruns::Line;
using textruns::Run;

textruns::Style styleOf(QColor plain, QColor strong)
{
    QFont body = fonts::body();
    body.setPixelSize(17);

    QFont bold = body;
    bold.setWeight(QFont::Bold);

    const Theme::TablePalette palette = Theme::dark().table;

    return { body, bold, plain, strong, palette.redSuit, palette.blackSuit };
}

textruns::Style style()
{
    const Theme::TablePalette palette = Theme::dark().table;

    return styleOf(palette.muted, palette.cream);
}

// How many pixels of `image` are within a few levels of `colour`, opaque enough to
// be the glyph and not its anti-aliased edge.
int pixelsNear(const QImage& image, const QColor& colour)
{
    int count = 0;

    for(int y = 0 ; y < image.height() ; y++)
    {
        for(int x = 0 ; x < image.width() ; x++)
        {
            const QColor pixel = image.pixelColor(x, y);

            if(pixel.alpha() > 200
               && std::abs(pixel.red() - colour.red()) <= 8
               && std::abs(pixel.green() - colour.green()) <= 8
               && std::abs(pixel.blue() - colour.blue()) <= 8)
            {
                count++;
            }
        }
    }

    return count;
}

QImage painted(const Line& line, const textruns::Style& look)
{
    QImage image(240, 40, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    textruns::paint(painter, QPointF(4.0, 28.0), line, look);
    painter.end();

    return image;
}

}

TEST_CASE("A line reads as its runs one after another", "[text-runs]")
{
    const Line line{ Run{ "Bids so far: " }, Run{ "3", Emphasis::Strong }, Run{ " of 6 tricks" } };

    REQUIRE(textruns::text(line) == QStringLiteral("Bids so far: 3 of 6 tricks"));
}

TEST_CASE("A strong run is measured in its own font", "[text-runs]")
{
    // Bold is wider, so a line measured all plain would be centred off by the
    // difference.
    const Line plain{ Run{ "Bogdan" } };
    const Line strong{ Run{ "Bogdan", Emphasis::Strong } };

    REQUIRE(textruns::width(strong, style()) > textruns::width(plain, style()));
}

TEST_CASE("A heart is red whatever colour its run is", "[text-runs]")
{
    // .red, inside the trick panel's gold-bright prompt or a muted row alike. The
    // runs are drawn in a colour far from red, so any red is the glyph's own.
    const Theme::TablePalette palette = Theme::dark().table;
    const textruns::Style look = styleOf(palette.goldBright, palette.cream);

    REQUIRE(pixelsNear(painted({ Run{ "Led ♥" } }, look), palette.redSuit) > 10);

    // And a spade is cream, not the run's gold.
    const QImage spade = painted({ Run{ "Led ♠" } }, look);

    REQUIRE(pixelsNear(spade, palette.blackSuit) > 10);
    REQUIRE(pixelsNear(spade, palette.redSuit) == 0);
}

TEST_CASE("A wrapped line breaks at spaces and fits its width", "[text-runs]")
{
    const Line line{ Run{ "Your turn. No " }, Run{ "♥" }, Run{ ", so you must trump." } };
    const textruns::Style look = style();

    const qreal width = textruns::width(line, look) * 0.6;
    const std::vector<Line> lines = textruns::wrap(line, look, width);

    REQUIRE(lines.size() == 2);

    // Nothing is lost but the space it broke at.
    REQUIRE(textruns::text(lines[0]) + QLatin1Char(' ') + textruns::text(lines[1])
            == textruns::text(line));

    for(const Line& wrapped : lines)
    {
        REQUIRE(textruns::width(wrapped, look) <= width);
        REQUIRE_FALSE(textruns::text(wrapped).startsWith(QLatin1Char(' ')));
        REQUIRE_FALSE(textruns::text(wrapped).endsWith(QLatin1Char(' ')));
    }
}

TEST_CASE("A line that fits is not wrapped", "[text-runs]")
{
    const Line line{ Run{ "Leader: " }, Run{ "Bogdan", Emphasis::Strong } };
    const std::vector<Line> lines = textruns::wrap(line, style(), 1000.0);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == line);
}

TEST_CASE("A word wider than the line stands on its own", "[text-runs]")
{
    const Line line{ Run{ "Alexandrina-Maria wins the trick." } };
    const std::vector<Line> lines = textruns::wrap(line, style(), 20.0);

    REQUIRE(lines.size() == 4);
    REQUIRE(textruns::text(lines[0]) == QStringLiteral("Alexandrina-Maria"));
}

TEST_CASE("An icon is drawn in the colour asked for and inside its box", "[text-runs]")
{
    QImage image(48, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QColor gold(0xC9, 0xA4, 0x5C);
    const QRectF box(12.0, 12.0, 24.0, 24.0);

    QPainter painter(&image);
    icons::paint(painter, icons::Icon::RoundHistory, box, gold);
    painter.end();

    // The grid's outline runs along the box's 4-unit inset, so its strokes are
    // drawn; and nothing outside the box is.
    REQUIRE(pixelsNear(image, gold) > 20);

    for(int y = 0 ; y < image.height() ; y++)
    {
        for(int x = 0 ; x < image.width() ; x++)
        {
            if(!box.adjusted(-1.0, -1.0, 1.0, 1.0).contains(QPointF(x + 0.5, y + 0.5)))
                REQUIRE(image.pixelColor(x, y).alpha() == 0);
        }
    }
}
