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
// stops looking like a card and starts looking like a poster. Both are logical
// pixels - the device pixel ratio is CardRenderer's business, not the layout's.
constexpr qreal minimumCardHeight = 44.0;
constexpr qreal maximumCardHeight = 220.0;

// A per-seat hue rotation off the accent, so six seats are distinguishable
// without six literals. 47 degrees is a step that does not repeat or collide
// within the six seats the engine allows.
constexpr int seatHueStep = 47;

}

// Keyed on the colour it was painted with as well as the size, because the
// palette members above are public and a caller is free to assign one. Without
// the tint in the key, a recoloured Theme would keep serving the old pixmap and
// look like the assignment had not taken.
struct Theme::Cache
{
    QSize backgroundSize;
    QRgb backgroundTint = 0;
    QPixmap background;

    // ONE BOX SIZE AT A TIME, rather than a growing map keyed on the size. Every
    // seat's plaque reserves the same box at any given moment, so a second size
    // means the window was resized and the first is never asked for again -
    // exactly the case that would otherwise grow without bound for as long as a
    // drag lasted. CardRenderer solves the same problem with a QCache because
    // its sizes genuinely differ from each other within one frame; here they do
    // not, and discarding the old set is both simpler and more complete.
    QSize avatarBox;
    QRgb avatarTint = 0;
    QHash<unsigned int, QPixmap> avatars;
};

Theme Theme::light()
{
    Theme theme;
    theme.tableSurface = QColor(0x3D, 0x6B, 0x54);
    theme.seatPlaque = QColor(0xF4, 0xF3, 0xEF);
    theme.accent = QColor(0xE0, 0xA3, 0x2E);
    theme.textPrimary = QColor(0x1C, 0x1F, 0x1D);
    theme.dimmedOverlay = QColor(0x00, 0x00, 0x00, 110);
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

QPixmap Theme::background(QSize view) const
{
    if(view.isEmpty())
        return QPixmap();

    // No cache at all rather than a wrong one: a default-constructed Theme has
    // no Cache, and painting is still the right answer for it.
    if(!cache)
        return paintBackground(view);

    if(cache->background.isNull()
        || cache->backgroundSize != view
        || cache->backgroundTint != tableSurface.rgba())
    {
        cache->background = paintBackground(view);
        cache->backgroundSize = view;
        cache->backgroundTint = tableSurface.rgba();
    }

    return cache->background;
}

QPixmap Theme::avatar(unsigned int seat, QSize box) const
{
    if(box.isEmpty())
        return QPixmap();

    if(!cache)
        return paintAvatar(seat, box);

    if(cache->avatarBox != box || cache->avatarTint != seatPlaque.rgba())
    {
        cache->avatars.clear();
        cache->avatarBox = box;
        cache->avatarTint = seatPlaque.rgba();
    }

    auto found = cache->avatars.constFind(seat);
    if(found != cache->avatars.constEnd())
        return found.value();

    return *cache->avatars.insert(seat, paintAvatar(seat, box));
}

// A single desaturated green, subtly darker at the edges (section 18). No
// texture bitmap, and no photograph - but the caller cannot tell, which is the
// property that matters.
QPixmap Theme::paintBackground(QSize view) const
{
    QPixmap pixmap(view);
    pixmap.fill(tableSurface);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF centre(view.width() / 2.0, view.height() / 2.0);

    // Reaching past the corners, so the darkening is a vignette rather than a
    // visible disc with a rim.
    const qreal radius = std::hypot(view.width(), view.height()) * 0.62;

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
QPixmap Theme::paintAvatar(unsigned int seat, QSize box) const
{
    QPixmap pixmap(box);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const qreal width = box.width();
    const qreal height = box.height();

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
