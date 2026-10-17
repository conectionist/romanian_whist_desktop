#include "CardRenderer.h"

#include "paint/DevicePixels.h"

#include <QPainter>
#include <QString>
#include <QSvgRenderer>

#include <algorithm>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

namespace
{

// The art's aspect, from the SVG's own viewBox="0 0 167.0869141 242.6669922".
// The PNG set exports the same shape at 500x726, so the two agree; only this one
// is used. It is width over height, so a height multiplied by it is a width.
constexpr qreal cardAspect = 167.0869141 / 242.6669922;

// Section 18's illegal-card treatment: 45% opacity plus a SLIGHT desaturation.
// Slight is the operative word - the alternative section 18 rejects by name is
// grey-on-white, which at card size is a rectangle with no rank on it.
constexpr qreal dimmedOpacity = 0.45;
constexpr qreal dimmedDesaturation = 0.35;

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

int towardGrey(int channel, int grey)
{
    return qRound(channel * (1.0 - dimmedDesaturation) + grey * dimmedDesaturation);
}

QImage dimmed(const QImage& source)
{
    // NOT premultiplied. Scaling the alpha of a premultiplied pixel without
    // scaling its colour channels to match brightens it, which is the opposite
    // of what dimming is for.
    QImage image = source.convertToFormat(QImage::Format_ARGB32);

    for(int y = 0 ; y < image.height() ; y++)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));

        for(int x = 0 ; x < image.width() ; x++)
        {
            const QRgb pixel = line[x];
            const int grey = qGray(pixel);

            line[x] = qRgba(towardGrey(qRed(pixel), grey),
                            towardGrey(qGreen(pixel), grey),
                            towardGrey(qBlue(pixel), grey),
                            qRound(qAlpha(pixel) * dimmedOpacity));
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

QSize CardRenderer::sizeForHeight(qreal height) const
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

// Flat and calm, to match section 18, and proportioned like the faces so the two
// sit together: the art rounds its corners at about 4.1% of the card's width.
QImage CardRenderer::paintBack(QSize physical) const
{
    QImage image(physical, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal width = physical.width();
    const qreal height = physical.height();
    const qreal radius = width * 0.041;

    // THE ONE COLOUR LITERAL OUTSIDE Theme, and section 18.1 allows exactly
    // this: the rule is that colours live in Theme OR CardRenderer. It belongs
    // here rather than there because section 18 is explicit that cards do not
    // change colour in dark mode - a card is white in the dark too - so a back
    // that took a Theme would be offering a choice that must not be made.
    const QColor field(0x2B, 0x4A, 0x7A);

    const QRectF body(0.5, 0.5, width - 1.0, height - 1.0);

    painter.setPen(QPen(QColor(0, 0, 0, 90), std::max(1.0, width * 0.006)));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(body, radius, radius);

    const qreal inset = width * 0.06;
    const QRectF panel = body.adjusted(inset, inset, -inset, -inset);

    painter.setPen(Qt::NoPen);
    painter.setBrush(field);
    painter.drawRoundedRect(panel, radius * 0.6, radius * 0.6);

    const qreal hairline = width * 0.022;
    painter.setPen(QPen(QColor(255, 255, 255, 60), std::max(1.0, width * 0.008)));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(panel.adjusted(hairline, hairline, -hairline, -hairline),
                            radius * 0.4, radius * 0.4);

    return image;
}
