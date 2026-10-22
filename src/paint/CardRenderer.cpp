#include "CardRenderer.h"

#include "paint/DevicePixels.h"

#include <QBrush>
#include <QPainter>
#include <QPolygonF>
#include <QTransform>
#include <QString>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

namespace
{

// The art's aspect, from the SVG's own viewBox="0 0 167.0869141 242.6669922".
// The PNG set exports the same shape at 500x726, so the two agree; only this one
// is used. It is width over height, so a height multiplied by it is a width.
constexpr qreal cardAspect = 167.0869141 / 242.6669922;

// AN ILLEGAL CARD, DARKENED: page.html's .hcard.illegal .face, filter:
// brightness(0.55) saturate(0.55). The table redesign's phase 7 replaced section
// 18's 45% opacity with it, the owner's decision of 2026-10-20: the cards in a
// hand overlap by half, and a card you can see through shows the corner of the
// one beneath it. Opaque, the card still reads as itself, only duller - which is
// what section 18 asked of its "slight" desaturation, against grey-on-white.
constexpr qreal dimmedBrightness = 0.55;
constexpr qreal dimmedSaturation = 0.55;

// 32 MB of rasterized cards, in the kilobyte units the costs below are given in.
// A table draws at most the trick, one hand and a stack of backs at any moment,
// so this is several times the working set; what it is really sized against is
// a window being resized, where every frame asks for a size the cache has never
// seen and the oldest entries are the ones that will never be asked for again.
constexpr int cacheBudgetKb = 32 * 1024;

unsigned int indexOf(Card card)
{
    return static_cast<unsigned int>(card.rank) * 4u + static_cast<unsigned int>(card.suit);
}

// The back gets A BIT OF ITS OWN rather than a spare index in the face space.
//
// It used to be index 52, one past the last card, and that is a slot only while
// the engine has exactly 13 ranks and 4 suits: indexOf() is rank * 4 + suit, so
// a fourteenth Rank makes some real card's index 52 as well, and it is served
// the back's art out of the cache. THE FAILURE IS SILENT AND IT IS WRONG PIXELS,
// which is worse than the missing-file failure the switches below guard against.
//
// A static_assert cannot close it. C++ has no way to name "the last enumerator",
// so an assert has to spell Rank::Ace - and Rank::Ace is still 12 after a Joker
// is appended after it, which is exactly the edit that breaks this. Tried, and
// it compiled clean with the collision present. The engine has no Count
// sentinel either, and adding one would break the deliberate no-default switches
// below, which are the guard that does work.
//
// So the collision is removed instead of detected: a separate bit cannot be
// reached by any arithmetic over the enums, whatever the engine does to them.
// The back still lives in the same QCache under the same budget and the same
// eviction, which is what section 18.1's argument for back() was actually about.
constexpr int backBit = 60;

// ALL FOUR COMPONENTS section 18.1 names: card, size, state and dpr. Dropping
// any one of them is a real bug with a real symptom - dpr is the one the build
// order's own table lists, as "cards soften on a HiDPI display".
//
// Size and dpr are both in the key rather than the physical size they multiply
// out to, because they are not interchangeable: 100 logical at dpr 2 and 200
// logical at dpr 1 rasterize to the same 200 physical pixels and are laid out at
// different sizes on screen.
//
// dpr is quantized by devicepixels::ratioKey() rather than hashed as a float, to
// ten-thousandths. Since the table redesign's phase 2 it is the screen's ratio
// times the view's scale, which is continuous - see DevicePixels.h for why
// hundredths were not fine enough. 20 bits of it reach a ratio of 104.
//
// Bit layout: back@60, state@59, card@52-58, dpr@32-51, height@16-31, width@0-15.
quint64 cacheKey(unsigned int cardIndex, bool isBack, CardRenderer::State state, QSize size,
                 qreal dpr)
{
    const quint64 dprKey = static_cast<quint64>(devicepixels::ratioKey(dpr)) & 0xFFFFF;

    return (static_cast<quint64>(isBack ? 1 : 0) << backBit)
         | (static_cast<quint64>(state == CardRenderer::State::Dimmed ? 1 : 0) << 59)
         | (static_cast<quint64>(cardIndex & 0x7F) << 52)
         | (dprKey << 32)
         | (static_cast<quint64>(size.height() & 0xFFFF) << 16)
         | (static_cast<quint64>(size.width() & 0xFFFF));
}

// SPELLED OUT, because Card::toString() is not the filename. It gives "J", "Q",
// "K", "A" and one-letter suits - the compact form a terminal wants - and the
// files spell both ends: jack_of_hearts.svg, 10_of_clubs.svg.
//
// No default label in either switch, so adding a rank or a suit to the engine is
// a -Wswitch warning here rather than a card that silently fails to load. The
// fallback return sits after the switch instead, which is what Card::toString()
// itself does.
QString rankName(Rank rank)
{
    switch(rank)
    {
    case Rank::Two:   return QStringLiteral("2");
    case Rank::Three: return QStringLiteral("3");
    case Rank::Four:  return QStringLiteral("4");
    case Rank::Five:  return QStringLiteral("5");
    case Rank::Six:   return QStringLiteral("6");
    case Rank::Seven: return QStringLiteral("7");
    case Rank::Eight: return QStringLiteral("8");
    case Rank::Nine:  return QStringLiteral("9");
    case Rank::Ten:   return QStringLiteral("10");
    case Rank::Jack:  return QStringLiteral("jack");
    case Rank::Queen: return QStringLiteral("queen");
    case Rank::King:  return QStringLiteral("king");
    case Rank::Ace:   return QStringLiteral("ace");
    }

    return QString();
}

QString suitName(Suit suit)
{
    switch(suit)
    {
    case Suit::Hearts:   return QStringLiteral("hearts");
    case Suit::Diamonds: return QStringLiteral("diamonds");
    case Suit::Spades:   return QStringLiteral("spades");
    case Suit::Clubs:    return QStringLiteral("clubs");
    }

    return QString();
}

// The qrc aliases every file flat under the /cards prefix, so the png/ and svg/
// directories the sources live in are NOT part of the runtime path. Only the
// extension tells the two sets apart.
QString filePath(Card card)
{
    const QString rank = rankName(card.rank);
    const QString suit = suitName(card.suit);

    if(rank.isEmpty() || suit.isEmpty())
        return QString();

    return QStringLiteral(":/cards/%1_of_%2.svg").arg(rank, suit);
}

// One channel through CSS's saturate() (Filter Effects, feColorMatrix
// type="saturate"): it moves toward the pixel's luminance, keeping `amount` of its
// own distance from it. The matrix's nine weights are this, multiplied out.
int saturated(qreal own, qreal luminance, qreal amount)
{
    return std::clamp(qRound(luminance + (own - luminance) * amount), 0, 255);
}

QImage dimmed(const QImage& source)
{
    // PREMULTIPLIED IS FINE HERE, and it is what the art comes in: both filters are
    // linear with no constant term, so filtering a premultiplied pixel gives the
    // premultiplied filtered pixel. The alpha is left as it is - the card stays
    // opaque, which is the point of the change.
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    for(int y = 0 ; y < image.height() ; y++)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));

        for(int x = 0 ; x < image.width() ; x++)
        {
            const QRgb pixel = line[x];

            // brightness() first, as the filter list is applied left to right. It
            // only scales, so nothing goes out of range before saturate() clamps.
            const qreal red = qRed(pixel) * dimmedBrightness;
            const qreal green = qGreen(pixel) * dimmedBrightness;
            const qreal blue = qBlue(pixel) * dimmedBrightness;

            // The matrix's luminance weights.
            const qreal luminance = 0.213 * red + 0.715 * green + 0.072 * blue;

            line[x] = qRgba(saturated(red, luminance, dimmedSaturation),
                            saturated(green, luminance, dimmedSaturation),
                            saturated(blue, luminance, dimmedSaturation),
                            qAlpha(pixel));
        }
    }

    return image;
}

QSize physicalSize(QSize size, qreal dpr)
{
    return QSize(std::max(1, qRound(size.width() * dpr)),
                 std::max(1, qRound(size.height() * dpr)));
}

}

CardRenderer::CardRenderer()
{
    cache.setMaxCost(cacheBudgetKb);
}

// Out of line, where QSvgRenderer is a complete type for the map's unique_ptrs.
CardRenderer::~CardRenderer() = default;

QSize CardRenderer::sizeForHeight(qreal height)
{
    if(height <= 0.0)
        return QSize();

    return QSize(std::max(1, qRound(height * cardAspect)), std::max(1, qRound(height)));
}

QPixmap CardRenderer::face(Card card, QSize size, qreal dpr, State state) const
{
    if(size.isEmpty() || dpr <= 0.0)
        return QPixmap();

    const quint64 key = cacheKey(indexOf(card), false, state, size, dpr);

    // A copy rather than the pointer: the next insert may evict what this points
    // at, and a caller holding a dangling QPixmap* would find out about it a
    // frame later and somewhere else.
    if(const QPixmap* cached = cache.object(key))
        return *cached;

    misses++;

    QImage image = rasterizeFace(card, physicalSize(size, dpr));
    if(image.isNull())
        return QPixmap();

    if(state == State::Dimmed)
        image = dimmed(image);

    return finish(image, key, dpr);
}

QPixmap CardRenderer::back(QSize size, qreal dpr) const
{
    if(size.isEmpty() || dpr <= 0.0)
        return QPixmap();

    // The card index is unused when the back bit is set, so it is 0 rather than
    // a sentinel that would have to stay out of the faces' way.
    const quint64 key = cacheKey(0, true, State::Normal, size, dpr);

    if(const QPixmap* cached = cache.object(key))
        return *cached;

    misses++;

    return finish(paintBack(physicalSize(size, dpr)), key, dpr);
}

QPixmap CardRenderer::finish(const QImage& image, quint64 key, qreal dpr) const
{
    QPixmap pixmap = QPixmap::fromImage(image);

    // THE OTHER HALF OF THE HiDPI STORY. Rasterizing at size * dpr without this
    // produces a pixmap that is correct and then drawn at dpr times the size it
    // should be; setting this without rasterizing larger produces one that is
    // the right size and soft. Both halves live on this one path so neither can
    // be done alone.
    pixmap.setDevicePixelRatio(dpr);

    const int costKb = std::max<int>(1, static_cast<int>(
        (static_cast<qint64>(image.width()) * image.height() * 4) / 1024));

    // insert() takes ownership and DELETES the object if it will not fit, so
    // there is nothing to clean up on the false branch - and nothing to report
    // either. A card too large for the whole budget is still a card the caller
    // asked for; it just gets rendered again next time.
    cache.insert(key, new QPixmap(pixmap), costKb);

    return pixmap;
}

QImage CardRenderer::rasterizeFace(Card card, QSize physical) const
{
    std::unique_ptr<QSvgRenderer>& renderer = art[indexOf(card)];

    if(!renderer)
    {
        const QString path = filePath(card);
        if(path.isEmpty())
            return QImage();

        auto parsed = std::make_unique<QSvgRenderer>(path);
        if(!parsed->isValid())
            return QImage();

        renderer = std::move(parsed);
    }

    QImage image(physical, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    // Onto transparency, with no card-shaped background painted first: the art
    // draws its own white body with rounded corners and a hairline border, so
    // filling behind it would only square off the corners it just rounded.
    renderer->render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(physical)));

    return image;
}

// THE TABLE REDESIGN'S BACK, table.js backSvg(): a cream border, a burgundy field
// with a gold lattice turned 45 degrees and a small dot at every crossing, a gold
// frame round the lattice, and a burgundy oval with a gold diamond in the middle.
//
// PAINTED IN THE MOCK-UP'S OWN UNITS, its viewBox of 100 x 145, scaled to the
// image - so every number below is copied from backSvg() as it is written. The
// faces' aspect is 0.689 and the viewBox's 0.690; the difference is under a tenth
// of a pixel on any card the table draws, so the scale is simply taken per axis.
QImage CardRenderer::paintBack(QSize physical) const
{
    QImage image(physical, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(physical.width() / 100.0, physical.height() / 145.0);

    // THE COLOUR LITERALS OUTSIDE Theme, and section 18.1 allows exactly these:
    // the rule is that colours live in Theme OR CardRenderer. They belong here
    // rather than there because section 18 is explicit that cards do not change
    // colour with the scheme - a card is white in the dark too - so a back that
    // took a Theme would be offering a choice that must not be made.
    const QColor cream(0xF2, 0xE8, 0xD2);
    const QColor burgundy(0x7A, 0x1C, 0x1C);
    const QColor lattice(0xD2, 0xAC, 0x63, 140);   // opacity .55
    const QColor dot(0xA8, 0x39, 0x2B);
    const QColor gold(0xD8, 0xB5, 0x6A);

    painter.setPen(Qt::NoPen);
    painter.setBrush(cream);
    painter.drawRoundedRect(QRectF(0.0, 0.0, 100.0, 145.0), 7.0, 7.0);

    painter.setBrush(burgundy);
    painter.drawRoundedRect(QRectF(4.5, 4.5, 91.0, 136.0), 4.5, 4.5);

    // THE LATTICE IS A TEXTURE BRUSH, NOT A CLIP. backSvg()'s <pattern> is a 9-unit
    // tile turned 45 degrees about the viewBox's origin; a fill with a brush keeps
    // the rounded rect's edge anti-aliased, where Qt's raster engine would draw a
    // clip path's edge aliased (the redesign's phase 3 found it on the table).
    //
    // The tile is drawn at twice the pixels it covers, so the brush samples it down
    // rather than up and the hairlines stay soft rather than stepped.
    const qreal unitsPerPixel = 100.0 / std::max(1, physical.width());
    const int tilePixels = std::max(9, static_cast<int>(std::ceil(2.0 * 9.0 / unitsPerPixel)));

    QImage tile(tilePixels, tilePixels, QImage::Format_ARGB32_Premultiplied);
    tile.fill(burgundy);
    {
        QPainter tilePainter(&tile);
        tilePainter.setRenderHint(QPainter::Antialiasing);
        tilePainter.scale(tilePixels / 9.0, tilePixels / 9.0);

        tilePainter.setPen(QPen(lattice, 0.5));
        tilePainter.drawLine(QPointF(0.0, 4.5), QPointF(9.0, 4.5));
        tilePainter.drawLine(QPointF(4.5, 0.0), QPointF(4.5, 9.0));

        tilePainter.setPen(Qt::NoPen);
        tilePainter.setBrush(dot);
        tilePainter.drawEllipse(QPointF(4.5, 4.5), 1.4, 1.4);
    }

    QBrush pattern(tile);
    pattern.setTransform(QTransform().rotate(45.0).scale(9.0 / tilePixels, 9.0 / tilePixels));

    painter.setBrush(pattern);
    painter.setPen(QPen(gold, 1.0));
    painter.drawRoundedRect(QRectF(8.0, 8.0, 84.0, 129.0), 3.0, 3.0);

    const QPointF centre(50.0, 72.5);

    painter.setBrush(burgundy);
    painter.setPen(QPen(gold, 1.2));
    painter.drawEllipse(centre, 15.0, 21.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(gold);
    painter.drawPolygon(QPolygonF({ centre + QPointF(0.0, -10.0), centre + QPointF(6.0, 0.0),
                                    centre + QPointF(0.0, 10.0), centre + QPointF(-6.0, 0.0) }));

    // A DARK HAIRLINE ROUND THE EDGE, which backSvg() has not: the face art draws
    // its own, and without one the cream border fades into a light background (the
    // card art window in the light theme). In the racks, the mock-up's only backs,
    // rackCardEdge is drawn over it, as it is over a Forehead face's. In pixels,
    // as the faces' line is, and inside the image.
    //
    // ITS CORNERS ARE THE BODY'S, ON EACH AXIS: the body was drawn in units scaled
    // separately across and down, so its corner is 7 units of each, an ellipse at
    // any shape but 100:145 - and the line has to follow it there too.
    const qreal pixelsAcross = physical.width() / 100.0;
    const qreal pixelsDown = physical.height() / 145.0;
    const qreal hairline = std::max(1.0, physical.width() * 0.006);

    painter.resetTransform();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 90), hairline));
    painter.drawRoundedRect(QRectF(QPointF(0.0, 0.0), QSizeF(physical))
                                .adjusted(hairline / 2.0, hairline / 2.0, -hairline / 2.0, -hairline / 2.0),
                            7.0 * pixelsAcross - hairline / 2.0, 7.0 * pixelsDown - hairline / 2.0);

    return image;
}
