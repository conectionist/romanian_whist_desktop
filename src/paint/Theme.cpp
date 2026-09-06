#include "Theme.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace
{

// THE TABLE'S METRICS, all of them, and the derivation of the one that is not
// obvious.
//
// The hand row's share of the window height is a straight choice. The width
// limit is not: section 14 fans at most eight cards overlapping by 40% of a
// card's width, so the fan spans cardWidth * (1 + 7 * 0.6) = 5.2 card widths,
// and a playing card is roughly 0.69 as wide as it is tall - so a fan spans
// about 3.58 card HEIGHTS. Allowing it 55% of the window width gives the
// coefficient below.
//
// That 0.69 is deliberately NOT a constant here and is not read from the art.
// Section 18.1's rule is that every table metric derives from the card height,
// so that new art with a different aspect does not mean re-tuning the layout;
// the exact aspect belongs to CardRenderer::sizeForHeight(), which is the only
// thing that has to be right about it. What this file needs is a reserve, and a
// reserve computed once from a nominal card shape is the correct amount of
// precision for one.
constexpr qreal handHeightFraction = 0.22;
constexpr qreal fanWidthFraction = 0.55;
constexpr qreal fanSpanInCardHeights = 3.58;

// Below the floor a rank pip stops being readable; above the ceiling a card
// stops looking like a card and starts looking like a poster. Both are LOGICAL
// pixels: a metric is what the layout reserves, and the ratio it is rasterized
// at never changes what the table looks like, only how sharp it is.
constexpr qreal minimumCardHeight = 44.0;
constexpr qreal maximumCardHeight = 220.0;

// A per-seat hue rotation off the accent, so six seats are distinguishable
// without six literals. 47 degrees is a step that does not repeat or collide
// within the six seats the engine allows.
constexpr int seatHueStep = 47;

// A duplicate of CardRenderer's, floor included, and deliberately not shared
// with it: Theme must not acquire a dependency on the card art to be able to
// scale a rectangle. Three lines is the cheaper of the two.
QSize physicalSize(QSize size, qreal dpr)
{
    return QSize(std::max(1, qRound(size.width() * dpr)),
                 std::max(1, qRound(size.height() * dpr)));
}

// Quantized to hundredths rather than compared as a float, for the reason
// CardRenderer's cache key gives: the ratio arrives as 1, 1.25, 1.5 or 2 in
// practice, and two bit patterns that differ in the last place are the same
// ratio for this purpose.
int dprKey(qreal dpr)
{
    return qRound(dpr * 100.0);
}

}

// Keyed on the PALETTE it was painted from as well as the size, because the
// palette members above are public and a caller is free to assign one. Without
// the tint in the key, a recoloured Theme would keep serving the old pixmap and
// look like the assignment had not taken.
//
// The whole palette rather than the colours the painter beneath it reads today:
// see paletteKey(). A per-colour key was how the avatar cache came to miss
// accent, which paintAvatar() reads and seatPlaque alone did not cover.
struct Theme::Cache
{
    QSize backgroundSize;
    int backgroundDpr = 0;
    quint64 backgroundTint = 0;
    QPixmap background;

    // ONE BOX SIZE AT A TIME, rather than a growing map keyed on the size. Every
    // seat's plaque reserves the same box at any given moment, so a second size
    // means the window was resized and the first is never asked for again -
    // exactly the case that would otherwise grow without bound for as long as a
    // drag lasted. CardRenderer solves the same problem with a QCache because
    // its sizes genuinely differ from each other within one frame; here they do
    // not, and discarding the old set is both simpler and more complete.
    QSize avatarBox;
    int avatarDpr = 0;
    quint64 avatarTint = 0;
    QHash<unsigned int, QPixmap> avatars;
};

// Cheap enough to call on every lookup - five rgba() reads and a hash - which is
// what lets both caches ask "is this the palette I was painted from?" without
// either of them knowing what its painter reads.
quint64 Theme::paletteKey() const
{
    return qHashMulti(0, tableSurface.rgba(), seatPlaque.rgba(), accent.rgba(),
                      textPrimary.rgba(), dimmedOverlay.rgba());
}

Theme Theme::light()
{
    Theme theme;
    theme.tableSurface = QColor(0x3D, 0x6B, 0x54);
    theme.seatPlaque = QColor(0xF4, 0xF3, 0xEF);
    theme.accent = QColor(0xE0, 0xA3, 0x2E);
    theme.textPrimary = QColor(0x1C, 0x1F, 0x1D);
    theme.dimmedOverlay = QColor(0x00, 0x00, 0x00, 110);
    theme.bidOnTarget = QColor(0x2E, 0x7D, 0x4F);
    theme.bidOwing = QColor(0x8A, 0x6D, 0x1E);
    theme.bidOver = QColor(0xA8, 0x3F, 0x2C);
    theme.cache = std::make_shared<Cache>();
    return theme;
}

// The surface changes tone; the accent brightens just enough to hold its
// contrast against it. Nothing here is a recolouring of a card, which section 18
// is explicit about: a card is white in the dark too.
Theme Theme::dark()
{
    Theme theme;
    theme.tableSurface = QColor(0x23, 0x40, 0x2F);
    theme.seatPlaque = QColor(0x2A, 0x2E, 0x2C);
    theme.accent = QColor(0xF0, 0xB6, 0x46);
    theme.textPrimary = QColor(0xE8, 0xE6, 0xE1);
    theme.dimmedOverlay = QColor(0x00, 0x00, 0x00, 140);

    // Lifted rather than recoloured: the three states have to stay the same
    // three colours a player has already learned, and only bright enough to
    // hold against the darker plaque behind them.
    theme.bidOnTarget = QColor(0x4C, 0xB3, 0x7B);
    theme.bidOwing = QColor(0xD1, 0xA6, 0x3E);
    theme.bidOver = QColor(0xE0, 0x6A, 0x53);
    theme.cache = std::make_shared<Cache>();
    return theme;
}

// Unknown reads as Light rather than as an error. It is what a platform that
// does not report a scheme returns, which is a normal state and not a fault, and
// section 20's preference is what overrides the guess for anyone it guesses
// wrong for.
Theme Theme::forScheme(Qt::ColorScheme scheme)
{
    if(scheme == Qt::ColorScheme::Dark)
        return dark();

    return light();
}

qreal Theme::cardHeightFor(QSize view) const
{
    if(view.isEmpty())
        return minimumCardHeight;

    const qreal fromHeight = view.height() * handHeightFraction;
    const qreal fromWidth = view.width() * fanWidthFraction / fanSpanInCardHeights;

    return std::clamp(std::min(fromHeight, fromWidth), minimumCardHeight, maximumCardHeight);
}

// BOTH HALVES OF THE HiDPI STORY ON ONE PATH, which is the discipline
// CardRenderer::finish() spells out: rasterizing at size * dpr without setting
// the ratio produces a pixmap drawn at dpr times the size it should be, and
// setting the ratio without rasterizing larger produces one that is the right
// size and soft. Neither can be done alone from here.
QPixmap Theme::rasterize(QPixmap pixmap, qreal dpr)
{
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QPixmap Theme::background(QSize view, qreal dpr) const
{
    if(view.isEmpty() || dpr <= 0.0)
        return QPixmap();

    // No cache at all rather than a wrong one: a default-constructed Theme has
    // no Cache, and painting is still the right answer for it.
    if(!cache)
        return rasterize(paintBackground(physicalSize(view, dpr)), dpr);

    if(cache->background.isNull()
        || cache->backgroundSize != view
        || cache->backgroundDpr != dprKey(dpr)
        || cache->backgroundTint != paletteKey())
    {
        cache->background = rasterize(paintBackground(physicalSize(view, dpr)), dpr);
        cache->backgroundSize = view;
        cache->backgroundDpr = dprKey(dpr);
        cache->backgroundTint = paletteKey();
    }

    return cache->background;
}

QPixmap Theme::avatar(unsigned int seat, QSize box, qreal dpr) const
{
    if(box.isEmpty() || dpr <= 0.0)
        return QPixmap();

    if(!cache)
        return rasterize(paintAvatar(seat, physicalSize(box, dpr)), dpr);

    // A changed ratio discards the set exactly as a changed box does, and for
    // the same reason: the window moved to another screen, so every avatar in
    // the old set is one nothing will ask for again.
    if(cache->avatarBox != box
        || cache->avatarDpr != dprKey(dpr)
        || cache->avatarTint != paletteKey())
    {
        cache->avatars.clear();
        cache->avatarBox = box;
        cache->avatarDpr = dprKey(dpr);
        cache->avatarTint = paletteKey();
    }

    auto found = cache->avatars.constFind(seat);
    if(found != cache->avatars.constEnd())
        return found.value();

    return *cache->avatars.insert(seat,
                                  rasterize(paintAvatar(seat, physicalSize(box, dpr)), dpr));
}

// A single desaturated green, subtly darker at the edges (section 18). No
// texture bitmap, and no photograph - but the caller cannot tell, which is the
// property that matters.
//
// PHYSICAL PIXELS. Every coordinate below is a fraction of the box, so this is
// the same picture at whatever resolution it is handed and needs to know
// nothing about a device pixel ratio.
QPixmap Theme::paintBackground(QSize physical) const
{
    QPixmap pixmap(physical);
    pixmap.fill(tableSurface);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF centre(physical.width() / 2.0, physical.height() / 2.0);

    // Reaching past the corners, so the darkening is a vignette rather than a
    // visible disc with a rim.
    const qreal radius = std::hypot(physical.width(), physical.height()) * 0.62;

    QRadialGradient gradient(centre, radius, centre);
    gradient.setColorAt(0.0, tableSurface.lighter(112));
    gradient.setColorAt(0.6, tableSurface);
    gradient.setColorAt(1.0, tableSurface.darker(128));

    painter.fillRect(pixmap.rect(), gradient);

    return pixmap;
}

// A flat silhouette in a disc, filling the box it was given. Deliberately plain:
// section 18 is a calm table rather than a skeuomorphic casino, and this is the
// element most likely to be the first thing replaced by real art.
//
// PHYSICAL PIXELS, as paintBackground() above and for the same reason.
QPixmap Theme::paintAvatar(unsigned int seat, QSize physical) const
{
    QPixmap pixmap(physical);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const qreal width = physical.width();
    const qreal height = physical.height();

    painter.setBrush(seatPlaque);
    painter.drawEllipse(QRectF(0.0, 0.0, width, height));

    // Hue-rotated off the accent so the seats differ from each other without
    // six colour literals. An achromatic accent reports hue -1, which would
    // rotate to nothing, so it gets a starting hue instead.
    QColor tint = accent;
    const int baseHue = tint.hue() < 0 ? 40 : tint.hue();
    tint.setHsv((baseHue + static_cast<int>(seat) * seatHueStep) % 360,
                std::max(120, tint.saturation()),
                std::min(210, tint.value()));

    painter.setBrush(tint);

    // Head and shoulders, clipped to the disc so the shoulders read as a
    // silhouette inside it rather than a bar across it.
    QPainterPath disc;
    disc.addEllipse(QRectF(0.0, 0.0, width, height));
    painter.setClipPath(disc);

    painter.drawEllipse(QPointF(width * 0.5, height * 0.38), width * 0.17, width * 0.17);

    QPainterPath shoulders;
    shoulders.addEllipse(QPointF(width * 0.5, height * 0.96), width * 0.32, height * 0.30);
    painter.drawPath(shoulders);

    return pixmap;
}
