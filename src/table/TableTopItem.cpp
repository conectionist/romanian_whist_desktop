#include "TableTopItem.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Gradients.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"
#include "table/TableSpace.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QGradientStops>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>
#include <QTransform>
#include <QtMath>

#include <algorithm>

namespace
{

// THE REST OF page.html's NUMBERS FOR THE TABLE, which only this file reads.
// Section 2.4's radii are in TableSpace.h, where the items laid on the table can
// see them; these are how the table itself is painted.
//
// The rim's inset box-shadows: a 2-unit dark stroke over a 4-unit warm one, and an
// inner blur of 22.
constexpr qreal woodDarkWidth = 2.0;
constexpr qreal woodHighlightWidth = 4.0;
constexpr qreal woodInnerShadowBlur = 22.0;

// The felt's two inset blurs.
constexpr qreal feltShadowWideBlur = 60.0;
constexpr qreal feltShadowNearBlur = 18.0;

// The felt's vignette: radial-gradient(circle, ...) with its transparent stop at 45%.
constexpr qreal vignetteClear = 0.45;

// The logo plaque, in its own 270 x 142 frame before the 0.9 it is drawn at.
// .panel gives it the drop shadow and the two inset rings; .logo the rest.
constexpr qreal logoRadius = 14.0;
constexpr qreal logoBorderWidth = 2.0;
constexpr qreal logoInsetDark = 3.0;
constexpr qreal logoInsetLine = 4.0;
constexpr QPointF logoShadowOffset(0.0, 12.0);
constexpr qreal logoShadowBlur = 26.0;

// .logo::before: a 1-unit line, its box inset 6 into the padding box, rounded 9.
constexpr qreal logoInnerLineInset = 6.0;
constexpr qreal logoInnerLineRadius = 9.0;

// The two ornaments are 120 x 12 SVGs, stroked 1.2, centred over and under the
// lettering - the stack of the three being centred in the plaque.
constexpr qreal ornamentWidth = 120.0;
constexpr qreal ornamentHeight = 12.0;
constexpr qreal ornamentStroke = 1.2;

// .logo h1: 37 units, a line height of 1, letter-spacing 0.01em, and a drop shadow
// 2 units straight down.
constexpr int letterSize = 37;
constexpr qreal letterSpacing = 0.01 * letterSize;
constexpr qreal letterShadowDrop = 2.0;

// .logo .club: 22 units, 16 in from either side of the padding box.
constexpr int clubSize = 22;
constexpr qreal clubInset = 16.0;

// The gradient across the lettering, and where its middle stop is.
constexpr qreal letterGradientMiddle = 0.48;

// The logo's background: radial-gradient(ellipse at 50% 40%, ... 70%, ...).
constexpr QPointF logoGradientAt(0.5, 0.4);
constexpr qreal logoGradientMiddle = 0.7;

// Every circle on the table is about its centre.
QPainterPath disc(qreal radius)
{
    return paths::disc(tablespace::centre, radius);
}

QPainterPath ring(qreal inner, qreal outer)
{
    return paths::ring(tablespace::centre, inner, outer);
}

using paths::rounded;
using paths::roundedBand;

// A TEXTURE OVER A DISC, at the pixels it lands on.
//
// The source art is scaled once, with a box filter, to exactly the pixels the disc
// covers - QPainter would scale it bilinearly at the blit, and taking 1020 pixels
// down to 700 that way aliases the grain. Then it is a brush, mapped so one of its
// pixels is one pixel of the picture, and the disc is filled with it: a fill is
// anti-aliased where a clip path is not, and the rim's edge is the one place the
// whole table's outline shows.
//
// AND ITS CORNER ON A WHOLE PIXEL. The disc's corner falls wherever the ratio puts
// it - 105 units in at a ratio of 0.767 is 80.6 pixels - and a brush laid half a
// pixel off the grid is sampled between its texels, every one of them blurred
// into its neighbour. That is the grain going soft, measured against the mock-up
// as a fifth less edge in the wood. devicepixels::snapped() puts the corner on
// the picture's own pixel grid.
void fillWithTexture(QPainter& painter, const QPixmap& texture, qreal radius, qreal ratio)
{
    if(texture.isNull())
        return;

    const QRectF square = tablespace::squareAround(radius);

    const QSize pixels(std::max(1, qRound(square.width() * ratio)),
                       std::max(1, qRound(square.height() * ratio)));

    const QPixmap scaled = texture.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QPointF corner = devicepixels::snapped(painter, square.topLeft());

    QTransform placed = QTransform::fromTranslate(corner.x(), corner.y());
    placed.scale(1.0 / ratio, 1.0 / ratio);

    QBrush brush(scaled);
    brush.setTransform(placed);

    painter.fillPath(disc(radius), brush);
}

// THE WOOD, THE BEAD AND THE FELT, in page.html's paint order. CSS paints an
// element's outer shadows, then its background, then its inset shadows - the last
// one listed lowest - and then its children.
void paintTable(QPainter& painter, const Theme& theme, qreal ratio)
{
    const Theme::TablePalette& table = theme.table;

    // .wood: 0 0 70px rgba(0, 0, 0, 0.75), straight down from overhead light.
    // BENEATH the rim rather than round it, since the wood is opaque: one
    // anti-aliased edge at 510 instead of a ring's inner edge under the wood's.
    shadows::drawCircleShadow(painter, tablespace::centre, tablespace::woodRadius,
                              tablespace::tableShadowBlur, table.tableShadow,
                              shadows::Side::Beneath);

    // The rim, the texture over its base colour.
    painter.fillPath(disc(tablespace::woodRadius), table.woodBase);
    fillWithTexture(painter, theme.woodTexture(), tablespace::woodRadius, ratio);

    // Its inset shadows, bottom up: the blur of 22, the 4-unit highlight, the
    // 2-unit dark stroke - which leaves the highlight showing as a fine warm line
    // 2 units inside the edge.
    //
    // ALL THREE END ON THE WOOD'S OWN EDGE, so the rim's outermost pixels are
    // covered once by the wood and again by each of them. That only darkens those
    // pixels, toward the stroke over the shadow, which is the colour they are
    // anyway; making each layer a disc under the next would take moving the
    // texture's anchor and cutting the inset profile short, for no visible change.
    shadows::drawCircleShadow(painter, tablespace::centre, tablespace::woodRadius,
                              woodInnerShadowBlur, table.woodInnerShadow, shadows::Side::Inset);

    painter.fillPath(ring(tablespace::woodRadius - woodHighlightWidth, tablespace::woodRadius),
                     table.woodHighlight);
    painter.fillPath(ring(tablespace::woodRadius - woodDarkWidth, tablespace::woodRadius),
                     table.woodDark);

    // .felt's three spread shadows, which are the bead: 13 units of the dark brown
    // under 11 of the middle under 9 of the inner. DISCS RATHER THAN RINGS, so the
    // felt laid over them meets one anti-aliased edge, not two that let the wood
    // show through between them.
    painter.fillPath(disc(tablespace::beadOuterRadius), table.woodDark);
    painter.fillPath(disc(tablespace::beadMiddleRadius), table.beadMiddle);
    painter.fillPath(disc(tablespace::beadInnerRadius), table.beadInner);

    // The felt: its base colour, its texture, and the vignette over both.
    painter.fillPath(disc(tablespace::feltRadius), table.feltBase);
    fillWithTexture(painter, theme.feltTexture(), tablespace::feltRadius, ratio);

    // THE VIGNETTE IS SIZED TO THE FELT'S BOX, NOT TO THE FELT. radial-gradient(circle)
    // reaches farthest-corner, the corner of the 880 box, 622 out - so the felt's
    // edge at 440 shows the dark stop only seven tenths of the way in, at about
    // 0.18, not the 0.38 section 2.4 reads as the edge's.
    //
    // ONE TRANSPARENT STOP AT THE CLEAR POINT, as CSS writes it. CSS blends
    // gradient stops premultiplied, so its transparent middle fades the white out
    // and the black in rather than passing through grey - and so does Qt's
    // default ColorInterpolation (GradientsTests holds it to that), so a clear
    // stop of any colour does the same here.
    const QGradientStops vignette = { { 0.0, table.feltVignetteLight },
                                      { vignetteClear, QColor(Qt::transparent) },
                                      { 1.0, table.feltVignetteDark } };

    const QRectF feltBox = tablespace::squareAround(tablespace::feltRadius);

    painter.fillPath(disc(tablespace::feltRadius),
                     gradients::cssRadial(feltBox, QPointF(0.5, 0.5), gradients::Shape::Circle,
                                          vignette));

    // Its inset shadows, bottom up: the near one of 18, the wide one of 60.
    shadows::drawCircleShadow(painter, tablespace::centre, tablespace::feltRadius,
                              feltShadowNearBlur, table.feltInnerShadowNear, shadows::Side::Inset);
    shadows::drawCircleShadow(painter, tablespace::centre, tablespace::feltRadius,
                              feltShadowWideBlur, table.feltInnerShadowWide, shadows::Side::Inset);

    // .felt-line, the felt's child and so over all of it: a 1.5-unit border inside
    // a box of radius 416, so a stroke centred 0.75 inside that.
    const qreal lineCentre = tablespace::goldLineRadius - tablespace::goldLineWidth / 2.0;

    painter.save();
    painter.setPen(QPen(table.feltLine, tablespace::goldLineWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(tablespace::centre, lineCentre, lineCentre);
    painter.restore();
}

// One of the two SVG ornaments, at its top-left in the plaque's frame. Every part
// is stroked, as the SVG's stroke is on the whole of it, and the diamond and the
// dots are filled as well. Butt caps and mitred joins, SVG's defaults.
void paintOrnament(QPainter& painter, QPointF topLeft, bool wavy, const QColor& colour)
{
    painter.save();
    painter.translate(topLeft);

    QPen pen(colour, ornamentStroke);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setMiterLimit(4.0);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Over the lettering: M2 6 H48 M72 6 H118. Under it, the same two strokes as
    // waves: M2 6 C20 0 30 12 48 6 M72 6 C90 0 100 12 118 6.
    QPainterPath strokes;

    strokes.moveTo(2.0, 6.0);

    if(wavy)
        strokes.cubicTo(20.0, 0.0, 30.0, 12.0, 48.0, 6.0);
    else
        strokes.lineTo(48.0, 6.0);

    strokes.moveTo(72.0, 6.0);

    if(wavy)
        strokes.cubicTo(90.0, 0.0, 100.0, 12.0, 118.0, 6.0);
    else
        strokes.lineTo(118.0, 6.0);

    painter.drawPath(strokes);

    // M60 1 L65 6 L60 11 L55 6 Z, in both.
    QPainterPath diamond;

    diamond.moveTo(60.0, 1.0);
    diamond.lineTo(65.0, 6.0);
    diamond.lineTo(60.0, 11.0);
    diamond.lineTo(55.0, 6.0);
    diamond.closeSubpath();

    painter.setBrush(colour);
    painter.drawPath(diamond);

    // And over the lettering only, a dot either side of it.
    if(!wavy)
    {
        painter.drawEllipse(QPointF(50.0, 6.0), 1.6, 1.6);
        painter.drawEllipse(QPointF(70.0, 6.0), 1.6, 1.6);
    }

    painter.restore();
}

// THE LOGO PLAQUE (.panel.logo), in its own 270 x 142 frame, drawn at 0.9 of that
// about the table's centre.
void paintLogo(QPainter& painter, const Theme& theme)
{
    const Theme::TablePalette& table = theme.table;

    painter.save();
    painter.translate(tablespace::centre);
    painter.scale(tablespace::logoScale, tablespace::logoScale);
    painter.translate(-tablespace::logoBox.width() / 2.0, -tablespace::logoBox.height() / 2.0);

    const QRectF outer(QPointF(0.0, 0.0), tablespace::logoBox);
    const QRectF padding = outer.adjusted(logoBorderWidth, logoBorderWidth,
                                          -logoBorderWidth, -logoBorderWidth);
    const qreal paddingRadius = logoRadius - logoBorderWidth;

    // .panel's drop shadow, which section 2.4 leaves out: the logo is
    // class="panel logo", and .logo overrides the panel's background and border
    // but not its shadows.
    shadows::drawShapeShadow(painter, rounded(outer, logoRadius), logoShadowOffset,
                             logoShadowBlur, table.panelShadow);

    // The background, over the whole border box and positioned on the padding box,
    // as CSS lays a background.
    const QGradientStops green = { { 0.0, table.logoCentre },
                                   { logoGradientMiddle, table.logoMiddle },
                                   { 1.0, table.logoEdge } };

    painter.fillPath(rounded(outer, logoRadius),
                     gradients::cssRadial(padding, logoGradientAt, gradients::Shape::Ellipse,
                                          green));

    // .panel's inset rings, bottom up: 4 units of faint gold under 3 of near-black,
    // which leaves one fine gold line 3 units inside the border. Each ring is the
    // padding box less itself shrunk by the spread, its corners shrunk with it.
    painter.fillPath(roundedBand(padding, paddingRadius,
                                 padding.adjusted(logoInsetLine, logoInsetLine,
                                                  -logoInsetLine, -logoInsetLine),
                                 paddingRadius - logoInsetLine),
                     table.panelInnerLine);

    painter.fillPath(roundedBand(padding, paddingRadius,
                                 padding.adjusted(logoInsetDark, logoInsetDark,
                                                  -logoInsetDark, -logoInsetDark),
                                 paddingRadius - logoInsetDark),
                     table.panelInnerShadow);

    // The border, over the inset rings as CSS paints it.
    painter.fillPath(roundedBand(outer, logoRadius, padding, paddingRadius), table.logoBorder);

    // .logo::before: a 1-unit line, so its stroke runs half a unit inside its box.
    const qreal lineInset = logoInnerLineInset + 0.5;

    painter.save();
    painter.setPen(QPen(table.logoInnerLine, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(padding.adjusted(lineInset, lineInset, -lineInset, -lineInset),
                            logoInnerLineRadius - 0.5, logoInnerLineRadius - 0.5);
    painter.restore();

    // THE STACK: an ornament, two lines of lettering, an ornament - 98 units tall,
    // centred in the padding box as the plaque's grid centres it.
    const qreal lettering = 2.0 * letterSize;
    const qreal stack = ornamentHeight + lettering + ornamentHeight;
    const qreal stackTop = padding.top() + (padding.height() - stack) / 2.0;
    const qreal ornamentLeft = padding.center().x() - ornamentWidth / 2.0;

    paintOrnament(painter, QPointF(ornamentLeft, stackTop), false, table.logoOrnament);
    paintOrnament(painter, QPointF(ornamentLeft, stackTop + ornamentHeight + lettering), true,
                  table.logoOrnament);

    // THE LETTERING, MIXED CASE AS page.html WRITES IT: Cinzel Decorative draws a
    // lowercase letter as a small capital, so "Romanian" is a tall R and smaller
    // capitals, where "ROMANIAN" would be eight tall ones. letter-spacing is a
    // fixed 0.37 after every letter - an absolute spacing, not Qt's percentage of
    // each letter's own width.
    QFont font = fonts::logo();
    font.setPixelSize(letterSize);
    font.setWeight(QFont::Bold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);

    const QFontMetricsF metrics(font);

    const QString lines[] = {
        QCoreApplication::translate("TableTopItem", "Romanian",
                                    "The logo on the table, first line"),
        QCoreApplication::translate("TableTopItem", "Whist",
                                    "The logo on the table, second line") };

    QPainterPath letters;
    qreal lineTop = stackTop + ornamentHeight;

    for(const QString& line : lines)
    {
        const qreal width = metrics.horizontalAdvance(line);

        letters.addText(QPointF(padding.center().x() - width / 2.0,
                                fonts::baselineIn(metrics, lineTop, letterSize)),
                        font, line);

        lineTop += letterSize;
    }

    // drop-shadow(0 2px 0) first, then the gold - ONE gradient down both lines, as
    // the h1 is one box: the top line is the lighter gold, the bottom the darker.
    painter.fillPath(letters.translated(0.0, letterShadowDrop), table.logoTextShadow);

    const qreal letteringTop = stackTop + ornamentHeight;

    QLinearGradient gold(0.0, letteringTop, 0.0, letteringTop + lettering);
    gold.setColorAt(0.0, table.logoTextTop);
    gold.setColorAt(letterGradientMiddle, table.logoTextMiddle);
    gold.setColorAt(1.0, table.logoTextBottom);

    painter.fillPath(letters, gold);

    // THE CLUBS, in the body face as the mock-up's spans are - which has no club, so
    // the glyph is whatever face the platform falls back to, here as in a browser.
    // Each sits 16 units in from the padding box, centred down it.
    QFont clubFont = fonts::body();
    clubFont.setPixelSize(clubSize);

    const QFontMetricsF clubMetrics(clubFont);
    const QString club = QStringLiteral("♣");

    // Its ascent-plus-descent centred on the padding box's middle.
    const qreal clubBaseline = padding.center().y()
                             - (clubMetrics.ascent() + clubMetrics.descent()) / 2.0
                             + clubMetrics.ascent();

    QPainterPath clubs;
    clubs.addText(QPointF(padding.left() + clubInset, clubBaseline), clubFont, club);
    clubs.addText(QPointF(padding.right() - clubInset - clubMetrics.horizontalAdvance(club),
                          clubBaseline),
                  clubFont, club);

    painter.fillPath(clubs, table.logoOrnament);

    painter.restore();
}

}

QRectF tabletop::bounds()
{
    return tablespace::squareAround(tablespace::tableReach);
}

QPixmap tabletop::render(const Theme& theme, qreal ratio)
{
    if(ratio <= 0.0)
        return QPixmap();

    const QRectF box = bounds();

    // Whole pixels, rounded UP so the picture is never short of its box; it is
    // drawn from the box's corner, so the fraction over falls past the shadow's
    // reach, where there is nothing to see.
    QPixmap picture(QSize(qCeil(box.width() * ratio), qCeil(box.height() * ratio)));

    // THE RATIO ON THE PIXMAP, so the painter below works in table units and every
    // pixel it lays down is a pixel of the screen - and so devicepixels, asked by
    // the shadows, answers with this ratio.
    picture.setDevicePixelRatio(ratio);
    picture.fill(Qt::transparent);

    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(-box.topLeft());

    paintTable(painter, theme, ratio);
    paintLogo(painter, theme);

    return picture;
}

TableTopItem::TableTopItem()
{
    setZValue(tablespace::z::table);

    // Under everything, and nothing to click: a press here goes to the scene.
    setAcceptedMouseButtons(Qt::NoButton);
}

void TableTopItem::setTheme(const Theme& palette)
{
    theme = palette;
    update();
}

QRectF TableTopItem::boundingRect() const
{
    return tabletop::bounds();
}

QPixmap TableTopItem::picture(qreal ratio) const
{
    const int key = devicepixels::ratioKey(ratio);

    if(cached.isNull() || cachedRatio != key || cachedPalette != theme.table)
    {
        cached = tabletop::render(theme, ratio);
        cachedRatio = key;
        cachedPalette = theme.table;
        renderCount++;
    }

    return cached;
}

void TableTopItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    // At the pixels it lands on - or, while the window is being resized, at the
    // scale the resize began from, which effectiveRatio() answers with on its own,
    // so the cached picture is drawn scaled rather than painted again on every
    // step of the drag (TableView).
    const QPixmap table = picture(devicepixels::effectiveRatio(*painter));

    if(table.isNull())
        return;

    painter->drawPixmap(tabletop::bounds().topLeft(), table);
}
