#include "Theme.h"

#include "paint/DevicePixels.h"

#include <QBrush>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QString>
#include <QTransform>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace
{

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

// The one place a texture is read, so the warning on a missing resource cannot
// be left off one of the two.
//
// THROUGH QImageReader, NOT QPixmap(path). The QPixmap constructor also files
// every load in the process-wide QPixmapCache, whose default budget is 10 MB and
// is shared with QStyle and QIcon. The two textures decode to about 7 MB of it,
// so they would evict the style's own pixmaps, and all to hold a second
// reference to pixels the Cache below already holds.
//
// A failed load is NOT remembered, so a missing resource warns on every call
// rather than once. Nothing about a missing resource gets better with time, so
// the repetition costs nothing - and a cache that stored the null would make
// the warning easy to scroll past and never see again.
QPixmap loadTexture(const QString& path)
{
    QImageReader reader(path);
    QImage image = reader.read();

    if(image.isNull())
    {
        qWarning("Theme: could not load %s (%s) - is table.qrc compiled into this binary?",
                 qUtf8Printable(path), qUtf8Printable(reader.errorString()));
        return QPixmap();
    }

    return QPixmap::fromImage(std::move(image));
}

// Both textures' share of the Cache, in one place. `slot` is that texture's
// member of it, or null for a Theme with no Cache, which loads every time - the
// same fallback background() makes.
QPixmap cachedTexture(QPixmap* slot, const QString& path)
{
    if(!slot)
        return loadTexture(path);

    if(slot->isNull())
        *slot = loadTexture(path);

    return *slot;
}

// WHAT A CACHED PICTURE WAS PAINTED FOR: a size, a ratio and the colours. One
// struct, compared whole by its own operator==, so the day the key gains a field
// it gains it for every cache below at once - which is the drift the note on
// Theme::Cache records happening twice when each cache spelled its key out.
//
// ALL OF ThemeColours, not the colours the painter beneath it reads today. Which
// members a painter depends on is not visible from the cache lookup, and the
// answer changes: a per-colour key was how the avatar cache came to miss accent,
// which paintAvatar() reads and seatPlaque alone did not cover. A copy of the
// colours rather than a hash of them, so the comparison can neither collide nor
// miss a member.
//
// The ratio through devicepixels::ratioKey() rather than compared as a float, so
// two bit patterns of the same ratio are one key and the quantization is the one
// CardRenderer's cache uses.
struct PictureKey
{
    QSize size;
    int dpr = 0;
    ThemeColours colours;

    bool operator==(const PictureKey&) const = default;
};

PictureKey keyFor(QSize size, qreal dpr, const ThemeColours& colours)
{
    return PictureKey{ size, devicepixels::ratioKey(dpr), colours };
}

// One picture and the key it was painted for. Held one at a time, because there
// is one table page and one main menu, each with one viewport.
struct CachedPicture
{
    PictureKey key;
    QPixmap pixmap;
};

// THE LOOKUP background() AND room() SHARE. `draw` paints and rasterizes on a
// miss; it is the caller's because the painters are Theme's private members. A
// null slot is a Theme with no Cache, which paints every time.
template<typename Draw>
QPixmap cachedPicture(CachedPicture* slot, const PictureKey& key, Draw draw)
{
    if(!slot)
        return draw();

    if(slot->pixmap.isNull() || slot->key != key)
        *slot = CachedPicture{ key, draw() };

    return slot->pixmap;
}

// Theme's layout with nothing added to the colours but the cache pointer, for
// the assertion below.
struct ColoursAndCache : ThemeColours
{
    std::shared_ptr<void> cache;
};

}

// A MEMBER DECLARED IN Theme RATHER THAN IN ThemeColours FAILS HERE. The caches
// compare ThemeColours and nothing else, so a colour added to Theme itself
// would be one a recolour could change while every cached picture stayed put.
static_assert(sizeof(Theme) == sizeof(ColoursAndCache),
              "Theme holds nothing but its cache: put a colour in ThemeColours (Theme.h)");

// Keyed on the COLOURS it was painted from as well as the size, because the
// colour members are public and a caller is free to assign one. Without them in
// the key, a recoloured Theme would keep serving the old pixmap and look like
// the assignment had not taken. PictureKey above says why it is all of them.
struct Theme::Cache
{
    CachedPicture background;

    // The redesign's room, held exactly as the background above is.
    CachedPicture room;

    // ONE BOX SIZE AT A TIME, rather than a growing map keyed on the size. Every
    // seat's plaque reserves the same box at any given moment, so a second size
    // means the window was resized and the first is never asked for again -
    // exactly the case that would otherwise grow without bound for as long as a
    // drag lasted. CardRenderer solves the same problem with a QCache because
    // its sizes genuinely differ from each other within one frame; here they do
    // not, and discarding the old set is both simpler and more complete.
    PictureKey avatarKey;
    QHash<unsigned int, QPixmap> avatars;

    // Keyed on nothing. A texture is source art, not a colour and not a size,
    // so the first load is the only one this cache ever needs to make.
    QPixmap wood;
    QPixmap felt;
};

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

    return cachedPicture(cache ? &cache->background : nullptr, keyFor(view, dpr, *this),
                         [&] { return rasterize(paintBackground(physicalSize(view, dpr)), dpr); });
}

QPixmap Theme::room(QSize view, qreal dpr) const
{
    if(view.isEmpty() || dpr <= 0.0)
        return QPixmap();

    return cachedPicture(cache ? &cache->room : nullptr, keyFor(view, dpr, *this),
                         [&] { return rasterize(paintRoom(physicalSize(view, dpr)), dpr); });
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
    const PictureKey key = keyFor(box, dpr, *this);

    if(cache->avatarKey != key)
    {
        cache->avatars.clear();
        cache->avatarKey = key;
    }

    auto found = cache->avatars.constFind(seat);
    if(found != cache->avatars.constEnd())
        return found.value();

    return *cache->avatars.insert(seat,
                                  rasterize(paintAvatar(seat, physicalSize(box, dpr)), dpr));
}

QPixmap Theme::woodTexture() const
{
    return cachedTexture(cache ? &cache->wood : nullptr, QStringLiteral(":/table/table_wood.png"));
}

QPixmap Theme::feltTexture() const
{
    return cachedTexture(cache ? &cache->felt : nullptr, QStringLiteral(":/table/table_felt.png"));
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

// page.html's #viewport: radial-gradient(ellipse at 50% 42%, #3b2414 0%,
// #1c110a 58%, #0b0704 100%).
//
// THE ELLIPSE IS CSS'S DEFAULT SIZE, farthest-corner, which the declaration does
// not spell out. CSS sizes it with the aspect of farthest-side - half the width
// across, and the 58% of the height below a centre at 42% - and grows that until
// it passes through the farthest corner. The two fractions then sum their
// squares to 2, so both radii are farthest-side's times the square root of two.
// A circle, or radii of half the box, would put the darkest band well inside the
// window rather than at its corners.
//
// QRadialGradient is a circle, so the ellipse is a unit circle in a brush
// transform scaled to the two radii.
//
// PHYSICAL PIXELS, as paintBackground() above and for the same reason.
QPixmap Theme::paintRoom(QSize physical) const
{
    QPixmap pixmap(physical);
    pixmap.fill(table.roomEdge);

    QPainter painter(&pixmap);

    const qreal width = physical.width();
    const qreal height = physical.height();

    QRadialGradient gradient(QPointF(0.0, 0.0), 1.0);
    gradient.setColorAt(0.0, table.roomCentre);
    gradient.setColorAt(0.58, table.roomMiddle);
    gradient.setColorAt(1.0, table.roomEdge);

    QTransform ellipse = QTransform::fromTranslate(width * 0.5, height * 0.42);
    ellipse.scale(std::sqrt(2.0) * width * 0.5, std::sqrt(2.0) * height * 0.58);

    QBrush brush(gradient);
    brush.setTransform(ellipse);

    painter.fillRect(pixmap.rect(), brush);

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
