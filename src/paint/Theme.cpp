#include "Theme.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <iterator>

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

// CSS's spelling, so each value below can be checked against the mock-up's
// page.html by eye: rgba(40, 29, 19, 0.95) there is rgba(40, 29, 19, 0.95) here.
QColor rgba(int red, int green, int blue, qreal alpha)
{
    QColor colour(red, green, blue);
    colour.setAlphaF(alpha);
    return colour;
}

// The redesign's palette, in ONE function that light() and dark() both call -
// which is what makes "the table page is dark in both schemes" true by
// construction rather than by two copies agreeing. Section 2.2 of
// docs/ui-redesign/IMPLEMENTATION_PLAN.md, in its order.
Theme::TablePalette tablePalette()
{
    Theme::TablePalette table;

    // radial-gradient(ellipse at 50% 42%, #3b2414 0%, #1c110a 58%, #0b0704 100%)
    table.roomCentre = QColor(0x3B, 0x24, 0x14);
    table.roomMiddle = QColor(0x1C, 0x11, 0x0A);
    table.roomEdge = QColor(0x0B, 0x07, 0x04);

    table.woodBase = QColor(0x5A, 0x33, 0x1A);
    table.feltBase = QColor(0x1D, 0x5A, 0x3A);

    table.panelTop = rgba(40, 29, 19, 0.95);
    table.panelBottom = rgba(19, 13, 8, 0.95);
    table.panelBorder = QColor(0x80, 0x61, 0x37);
    table.panelInnerShadow = rgba(18, 12, 7, 0.9);
    table.panelInnerLine = rgba(201, 164, 92, 0.3);
    table.panelShadow = rgba(0, 0, 0, 0.55);

    table.gold = QColor(0xC9, 0xA4, 0x5C);
    table.goldBright = QColor(0xF2, 0xD3, 0x8C);
    table.cream = QColor(0xF1, 0xE6, 0xCC);
    table.muted = QColor(0xBB, 0xA7, 0x83);

    table.good = QColor(0x86, 0xC9, 0x8F);
    table.warn = QColor(0xE6, 0xB9, 0x5C);
    table.bad = QColor(0xE5, 0x80, 0x6D);

    // Assigned rather than repeated, as dark()'s accentText is: the black suits
    // ARE the text colour, and a second copy of it is a second thing to keep in
    // step.
    table.redSuit = QColor(0xE0, 0x52, 0x4A);
    table.blackSuit = table.cream;

    table.buttonTop = QColor(0x2A, 0x61, 0x42);
    table.buttonBottom = QColor(0x17, 0x40, 0x2A);
    table.buttonBorder = QColor(0xA3, 0x82, 0x4A);

    return table;
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

    // Keyed on nothing. A texture is source art, not a colour and not a size,
    // so the first load is the only one this cache ever needs to make.
    QPixmap wood;
    QPixmap felt;
};

// Cheap enough to call on every lookup - a few dozen rgba() reads and a hash -
// which is what lets both caches ask "is this the palette I was painted from?"
// without either of them knowing what its painter reads.
//
// EVERY MEMBER OF THE STRUCT, including the three no painter reads yet. B2 added
// the bid colours here and left this at five, which is the same omission
// paintAvatar() made with accent one phase earlier - so the rule is restated
// rather than relied on: a colour added above is added here in the same edit,
// whether or not anything paints it today.
//
// An array rather than one long qHashMulti() call, since the redesign's
// palette more than tripled the count: one colour per line is a list a reader
// can check against the struct, which an argument list of thirty-four is not.
// ThemeTests checks it as well, for every `table` member, by count.
quint64 Theme::paletteKey() const
{
    const QRgb colours[] = {
        tableSurface.rgba(),
        seatPlaque.rgba(),
        accent.rgba(),
        accentText.rgba(),
        textPrimary.rgba(),
        dimmedOverlay.rgba(),
        bidOnTarget.rgba(),
        bidOwing.rgba(),
        bidOver.rgba(),
        scorePositive.rgba(),
        scoreNegative.rgba(),

        table.roomCentre.rgba(),
        table.roomMiddle.rgba(),
        table.roomEdge.rgba(),
        table.woodBase.rgba(),
        table.feltBase.rgba(),
        table.panelTop.rgba(),
        table.panelBottom.rgba(),
        table.panelBorder.rgba(),
        table.panelInnerShadow.rgba(),
        table.panelInnerLine.rgba(),
        table.panelShadow.rgba(),
        table.gold.rgba(),
        table.goldBright.rgba(),
        table.cream.rgba(),
        table.muted.rgba(),
        table.good.rgba(),
        table.warn.rgba(),
        table.bad.rgba(),
        table.redSuit.rgba(),
        table.blackSuit.rgba(),
        table.buttonTop.rgba(),
        table.buttonBottom.rgba(),
        table.buttonBorder.rgba(),
    };

    return qHashRange(std::begin(colours), std::end(colours));
}

Theme Theme::light()
{
    Theme theme;
    theme.tableSurface = QColor(0x3D, 0x6B, 0x54);
    theme.seatPlaque = QColor(0xF4, 0xF3, 0xEF);
    theme.accent = QColor(0xE0, 0xA3, 0x2E);

    // The same amber walked down until it reads as words on the plaque above:
    // 5.4:1 against seatPlaque, where the accent itself is 2.0:1. Darkened along
    // its own hue rather than swapped for a brown, so the called-out round is
    // recognisably the accent colour and not a second one to learn.
    theme.accentText = QColor(0x7A, 0x60, 0x19);

    theme.textPrimary = QColor(0x1C, 0x1F, 0x1D);
    theme.dimmedOverlay = QColor(0x00, 0x00, 0x00, 110);
    theme.bidOnTarget = QColor(0x2E, 0x7D, 0x4F);

    // DARKENED IN C4a, from 0x8A6D1E, and by a hair rather than by design: the
    // contrast case added with accentText measured the old amber at 4.41:1 on
    // the plaque, which is under AA by less than a tenth and had been since B2.
    // Same hue, five percent less light, 5.1:1 - the pill state a player learned
    // is the pill state they still see.
    theme.bidOwing = QColor(0x7F, 0x64, 0x1A);

    theme.bidOver = QColor(0xA8, 0x3F, 0x2C);
    theme.scorePositive = QColor(0x1E, 0x6B, 0x3F);
    theme.scoreNegative = QColor(0x9C, 0x2F, 0x22);

    // The dark table in the light theme too - see Theme.h.
    theme.table = tablePalette();

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

    // ONE COLOUR HERE, and the header says why: the lift this accent already
    // took to hold against a dark plaque is the same lift text needs, so it
    // measures 7.5:1 as it stands. Assigned rather than repeated as a literal,
    // because a second copy of the value is a second thing to keep in step.
    theme.accentText = theme.accent;

    theme.textPrimary = QColor(0xE8, 0xE6, 0xE1);
    theme.dimmedOverlay = QColor(0x00, 0x00, 0x00, 140);

    // Lifted rather than recoloured: the three states have to stay the same
    // three colours a player has already learned, and only bright enough to
    // hold against the darker plaque behind them.
    theme.bidOnTarget = QColor(0x4C, 0xB3, 0x7B);
    theme.bidOwing = QColor(0xD1, 0xA6, 0x3E);

    // LIFTED FURTHER IN C4a, from 0xE06A53. The paragraph above is the rule and
    // this was the one of the three that did not quite follow it: the red came
    // out at 4.17:1 on the dark plaque where the green and the amber cleared 5,
    // which is what "only bright enough" reads as when it is judged by eye. Same
    // red, four percent more light, 4.7:1.
    theme.bidOver = QColor(0xE3, 0x79, 0x64);
    theme.scorePositive = QColor(0x5F, 0xC1, 0x88);
    theme.scoreNegative = QColor(0xE8, 0x76, 0x5F);
    theme.table = tablePalette();
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

// A failed load is NOT remembered, so a missing resource warns on every call
// rather than once. Nothing about a missing resource gets better with time, so
// the repetition costs nothing - and a cache that stored the null would make
// the warning easy to scroll past and never see again.
QPixmap Theme::loadTexture(const QString& path)
{
    QPixmap pixmap(path);

    if(pixmap.isNull())
        qWarning("Theme: could not load %s - is table.qrc compiled into this binary?",
                 qUtf8Printable(path));

    return pixmap;
}

QPixmap Theme::woodTexture() const
{
    const QString path = QStringLiteral(":/table/table_wood.png");

    if(!cache)
        return loadTexture(path);

    if(cache->wood.isNull())
        cache->wood = loadTexture(path);

    return cache->wood;
}

QPixmap Theme::feltTexture() const
{
    const QString path = QStringLiteral(":/table/table_felt.png");

    if(!cache)
        return loadTexture(path);

    if(cache->felt.isNull())
        cache->felt = loadTexture(path);

    return cache->felt;
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
