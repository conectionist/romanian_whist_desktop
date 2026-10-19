#include "Theme.h"

#include "paint/DevicePixels.h"
#include "paint/Gradients.h"

#include <QGradientStops>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QSizeF>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace
{

// table.js portrait()'s viewBox, which every coordinate in paintAvatar() is in.
constexpr QSizeF portraitView(100.0, 114.0);

// page.html's .portrait filter: sepia(0.18) contrast(1.05), in that order, as the
// Filter Effects spec defines both - sepia a colour matrix interpolated from the
// identity by its amount, contrast a slope about mid-grey. On straight colour, in
// sRGB, as browsers apply the shorthand filters.
constexpr qreal portraitSepia = 0.18;
constexpr qreal portraitContrast = 1.05;

// One opaque colour through the filter. The picture's pixels go through it, and so
// does the frame's colour, because CSS filters the whole element, border and all.
QRgb portraitFiltered(QRgb colour)
{
    constexpr qreal k = 1.0 - portraitSepia;

    constexpr qreal m[3][3] = {
        { 0.393 + 0.607 * k, 0.769 - 0.769 * k, 0.189 - 0.189 * k },
        { 0.349 - 0.349 * k, 0.686 + 0.314 * k, 0.168 - 0.168 * k },
        { 0.272 - 0.272 * k, 0.534 - 0.534 * k, 0.131 + 0.869 * k }
    };

    const auto channel = [](qreal value)
    {
        const qreal contrasted = (value - 0.5) * portraitContrast + 0.5;
        return qBound(0, qRound(contrasted * 255.0), 255);
    };

    const qreal r = qRed(colour) / 255.0;
    const qreal g = qGreen(colour) / 255.0;
    const qreal b = qBlue(colour) / 255.0;

    return qRgba(channel(std::min(1.0, m[0][0] * r + m[0][1] * g + m[0][2] * b)),
                 channel(std::min(1.0, m[1][0] * r + m[1][1] * g + m[1][2] * b)),
                 channel(std::min(1.0, m[2][0] * r + m[2][1] * g + m[2][2] * b)),
                 qAlpha(colour));
}

void applyPortraitFilter(QImage& image)
{
    for(int y = 0 ; y < image.height() ; y++)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));

        // The picture is opaque - its background fills the box - so the
        // premultiplied pixel IS the straight one.
        for(int x = 0 ; x < image.width() ; x++)
            line[x] = portraitFiltered(line[x]);
    }
}

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

    // Section 2.4, from page.html's .wood, .felt, .felt-line and .logo.
    //
    // inset 0 0 0 2px #2a160a, inset 0 0 0 4px rgba(255, 210, 150, 0.12) - and the
    // felt's outermost bead ring, 0 0 0 13px #2a160a, is the same brown.
    table.woodDark = QColor(0x2A, 0x16, 0x0A);
    table.woodHighlight = rgba(255, 210, 150, 0.12);

    // 0 0 0 9px #3b210f, 0 0 0 11px #7a4c27
    table.beadInner = QColor(0x3B, 0x21, 0x0F);
    table.beadMiddle = QColor(0x7A, 0x4C, 0x27);

    // radial-gradient(circle, rgba(255, 255, 255, 0.07) 0%, rgba(0, 0, 0, 0) 45%,
    // rgba(0, 0, 0, 0.38) 100%); border: 1.5px solid rgba(201, 164, 92, 0.2)
    table.feltVignetteLight = rgba(255, 255, 255, 0.07);
    table.feltVignetteDark = rgba(0, 0, 0, 0.38);
    table.feltLine = rgba(201, 164, 92, 0.2);

    // 0 0 70px rgba(0, 0, 0, 0.75); inset 0 0 22px rgba(0, 0, 0, 0.5);
    // inset 0 0 60px rgba(0, 0, 0, 0.55), inset 0 0 18px rgba(0, 0, 0, 0.45)
    table.tableShadow = rgba(0, 0, 0, 0.75);
    table.woodInnerShadow = rgba(0, 0, 0, 0.5);
    table.feltInnerShadowWide = rgba(0, 0, 0, 0.55);
    table.feltInnerShadowNear = rgba(0, 0, 0, 0.45);

    // radial-gradient(ellipse at 50% 40%, #2a6142 0%, #17402a 70%, #0f2c1d 100%):
    // the first two ARE the buttons' green, assigned for the reason blackSuit is.
    table.logoCentre = table.buttonTop;
    table.logoMiddle = table.buttonBottom;
    table.logoEdge = QColor(0x0F, 0x2C, 0x1D);

    // border: 2px solid #b8924a; ::before border: 1px solid rgba(232, 200, 130, 0.55)
    table.logoBorder = QColor(0xB8, 0x92, 0x4A);
    table.logoInnerLine = rgba(232, 200, 130, 0.55);

    // linear-gradient(180deg, #fbe7b2 0%, #e2b866 48%, #a77a35 100%);
    // drop-shadow(0 2px 0 rgba(0, 0, 0, 0.55)); the clubs and ornaments #d9b46a
    table.logoTextTop = QColor(0xFB, 0xE7, 0xB2);
    table.logoTextMiddle = QColor(0xE2, 0xB8, 0x66);
    table.logoTextBottom = QColor(0xA7, 0x7A, 0x35);
    table.logoTextShadow = rgba(0, 0, 0, 0.55);
    table.logoOrnament = QColor(0xD9, 0xB4, 0x6A);

    // Section 2.5, from page.html's .plaque and .pill and table.js portrait().
    //
    // .plaque.active: 0 0 26px rgba(242, 211, 140, 0.5); .pill: border 1px solid
    // rgba(201, 164, 92, 0.4) - the gold of every other line, fainter.
    table.activeGlow = rgba(242, 211, 140, 0.5);
    table.pillBorder = rgba(201, 164, 92, 0.4);

    // .portrait: border 1px solid #a3824a, the buttons' gold, assigned for the
    // reason blackSuit is - and through the portrait's filter, which CSS applies to
    // the whole element, border included.
    table.portraitBorder = QColor::fromRgba(portraitFiltered(table.buttonBorder.rgba()));

    // The background's outer stop, the eyebrows' stroke and the plain mouth.
    table.portraitEdge = QColor(0x17, 0x10, 0x0A);
    table.portraitBrow = QColor(0x2A, 0x1A, 0x10);
    table.portraitMouth = QColor(0x8A, 0x4F, 0x3C);

    // Section 2.6, from page.html's .fan, .fan .back and .fan .ledge.
    //
    // .fan: filter: drop-shadow(0 5px 5px rgba(0, 0, 0, 0.55)).
    table.rackShadow = rgba(0, 0, 0, 0.55);

    // .back: linear-gradient(0deg, rgba(255, 240, 210, 0.16), transparent 45%,
    // rgba(0, 0, 0, 0.3)), and box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.25).
    table.rackCardLight = rgba(255, 240, 210, 0.16);
    table.rackCardDark = rgba(0, 0, 0, 0.3);
    table.rackCardEdge = rgba(0, 0, 0, 0.25);

    // .ledge: linear-gradient(180deg, rgba(255, 220, 170, 0.35), transparent 30%,
    // rgba(0, 0, 0, 0.45)) over the wood texture over #6b3d1f; box-shadow: 0 2px 3px
    // rgba(0, 0, 0, 0.6), inset 0 0 0 1px rgba(30, 14, 4, 0.6).
    //
    // NO TEXTURE. The mock-up centres the wood texture on the rail at 900 x 900, and
    // the texture is the rim - a ring, transparent in the middle - so what shows
    // there is the base colour alone. This is what the mock-up draws.
    table.railBase = QColor(0x6B, 0x3D, 0x1F);
    table.railLight = rgba(255, 220, 170, 0.35);
    table.railDark = rgba(0, 0, 0, 0.45);
    table.railEdge = rgba(30, 14, 4, 0.6);
    table.railShadow = rgba(0, 0, 0, 0.6);

    // Section 2.7, from page.html's .tcard and .leadpip.
    //
    // .tcard .inner: box-shadow 0 6px 14px rgba(0, 0, 0, 0.45). The winning one's:
    // 0 0 0 2px gold-bright, 0 0 26px rgba(242, 211, 140, 0.55), 0 16px 24px
    // rgba(0, 0, 0, 0.5) - its ring is goldBright above.
    table.playedShadow = rgba(0, 0, 0, 0.45);
    table.winningShadow = rgba(0, 0, 0, 0.5);
    table.winningGlow = rgba(242, 211, 140, 0.55);

    // .leadpip: background gold-bright, box-shadow 0 0 0 1px rgba(60, 40, 10, 0.5),
    // 0 0 10px rgba(242, 211, 140, 0.8).
    table.leadMarkOutline = rgba(60, 40, 10, 0.5);
    table.leadMarkGlow = rgba(242, 211, 140, 0.8);

    // table.js PLAYERS, in its order: bg, skin, shade, hair, coat, shirt, then the
    // optional hat, mustache and beard, and long hair.
    using Look = Theme::TablePalette::PortraitLook;
    const QColor none;

    table.portraits = { {
        // You: dark hair, a light beard.
        Look{ QColor(0x6B, 0x52, 0x33), QColor(0xD9, 0xAB, 0x86), QColor(0xB9, 0x8A, 0x68),
              QColor(0x2A, 0x1C, 0x14), QColor(0x3A, 0x35, 0x30), QColor(0xD8, 0xCF, 0xBF),
              none, none, QColor(0x3A, 0x2A, 0x1E), false },
        // A hat and a mustache.
        Look{ QColor(0x5A, 0x4A, 0x35), QColor(0xCF, 0x9F, 0x7C), QColor(0xA8, 0x7C, 0x5E),
              QColor(0x3B, 0x2A, 0x1C), QColor(0x4A, 0x35, 0x24), QColor(0xBF, 0xB3, 0x9E),
              QColor(0x3D, 0x3A, 0x36), QColor(0x3B, 0x2A, 0x1C), none, false },
        // Long brown hair.
        Look{ QColor(0x6E, 0x5A, 0x3E), QColor(0xE2, 0xB9, 0x97), QColor(0xC0, 0x95, 0x76),
              QColor(0x4A, 0x2F, 0x1D), QColor(0x4B, 0x33, 0x24), QColor(0xE8, 0xE0, 0xCF),
              none, none, none, true },
        // White hair, a hat and a white mustache.
        Look{ QColor(0x5F, 0x54, 0x40), QColor(0xD5, 0xA8, 0x88), QColor(0xAE, 0x84, 0x66),
              QColor(0xD8, 0xD2, 0xC6), QColor(0x3C, 0x3A, 0x32), QColor(0xCF, 0xC6, 0xB2),
              QColor(0x6A, 0x51, 0x34), QColor(0xEC, 0xE6, 0xDA), none, false },
        // Long red hair.
        Look{ QColor(0x5C, 0x46, 0x36), QColor(0xE8, 0xC2, 0xA2), QColor(0xC7, 0x9E, 0x80),
              QColor(0x8A, 0x3F, 0x22), QColor(0x2F, 0x3B, 0x34), QColor(0xE4, 0xDC, 0xCB),
              none, none, none, true },
        // Black hair and a beard.
        Look{ QColor(0x4F, 0x4A, 0x3C), QColor(0xC9, 0x98, 0x77), QColor(0xA3, 0x76, 0x5A),
              QColor(0x1F, 0x17, 0x12), QColor(0x5A, 0x4A, 0x32), QColor(0xD2, 0xC8, 0xB4),
              none, none, QColor(0x1F, 0x17, 0x12), false }
    } };

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
// not spell out - gradients::cssRadial() says what that means. A circle, or radii
// of half the box, would put the darkest band well inside the window rather than
// at its corners.
//
// PHYSICAL PIXELS, as paintBackground() above and for the same reason.
QPixmap Theme::paintRoom(QSize physical) const
{
    QPixmap pixmap(physical);
    pixmap.fill(table.roomEdge);

    QPainter painter(&pixmap);
    painter.fillRect(pixmap.rect(), roomBrush(QRectF(pixmap.rect())));

    return pixmap;
}

QBrush Theme::roomBrush(const QRectF& view) const
{
    const QGradientStops stops = { { 0.0, table.roomCentre },
                                   { 0.58, table.roomMiddle },
                                   { 1.0, table.roomEdge } };

    return gradients::cssRadial(view, QPointF(0.5, 0.42), gradients::Shape::Ellipse, stops);
}

// table.js portrait(): a head-and-shoulders bust on a dark vignette, drawn in the
// SVG's own 100 x 114 viewBox and fitted into the box as SVG fits one - scaled
// to the nearer side and centred (xMidYMid meet) - then given page.html's
// sepia(0.18) contrast(1.05). Every path below is the SVG's, command for command,
// so the mock-up is the document for each number.
//
// PHYSICAL PIXELS, as paintBackground() above and for the same reason.
QPixmap Theme::paintAvatar(unsigned int seat, QSize physical) const
{
    const TablePalette::PortraitLook& look = table.portraits[seat % table.portraits.size()];

    // An image rather than a pixmap, for the filter's pixel pass at the end.
    QImage image(physical, QImage::Format_ARGB32_Premultiplied);
    image.fill(table.portraitEdge);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const qreal scale = std::min(physical.width() / portraitView.width(),
                                 physical.height() / portraitView.height());

    painter.translate((physical.width() - portraitView.width() * scale) / 2.0,
                      (physical.height() - portraitView.height() * scale) / 2.0);
    painter.scale(scale, scale);

    const QRectF view(QPointF(0.0, 0.0), portraitView);

    // radialGradient cx 50% cy 36% r 72%, in the rect's bounding box - so it is an
    // ellipse as tall as the box is.
    QRadialGradient background(QPointF(0.5, 0.36), 0.72);
    background.setCoordinateMode(QGradient::ObjectBoundingMode);
    background.setColorAt(0.0, look.background);
    background.setColorAt(1.0, table.portraitEdge);
    painter.fillRect(view, background);

    if(look.longHair)
    {
        QPainterPath hair(QPointF(29, 46));
        hair.quadTo(24, 84, 32, 100);
        hair.lineTo(68, 100);
        hair.quadTo(76, 84, 71, 46);
        hair.closeSubpath();
        painter.fillPath(hair, look.hair);
    }

    QPainterPath coat(QPointF(6, 114));
    coat.quadTo(10, 82, 34, 77);
    coat.lineTo(66, 77);
    coat.quadTo(90, 82, 94, 114);
    coat.closeSubpath();
    painter.fillPath(coat, look.coat);

    QPainterPath shirt(QPointF(39, 77));
    shirt.lineTo(50, 94);
    shirt.lineTo(61, 77);
    shirt.closeSubpath();
    painter.fillPath(shirt, look.shirt);

    painter.setBrush(look.shade);
    painter.drawRoundedRect(QRectF(43, 62, 14, 17), 5, 5);

    painter.setBrush(look.skin);
    painter.drawEllipse(QPointF(50, 48), 16, 19.5);

    painter.setBrush(look.shade);
    painter.drawEllipse(QPointF(33.5, 50), 2.6, 4.4);
    painter.drawEllipse(QPointF(66.5, 50), 2.6, 4.4);

    QPainterPath hair(QPointF(34, 44));
    hair.quadTo(33, 26, 50, 25);
    hair.quadTo(67, 26, 66, 44);
    hair.quadTo(62, 34, 50, 33.5);
    hair.quadTo(38, 34, 34, 44);
    hair.closeSubpath();
    painter.fillPath(hair, look.hair);

    if(look.hat.isValid())
    {
        QPainterPath crown(QPointF(32, 38));
        crown.quadTo(33, 22, 50, 21);
        crown.quadTo(68, 22, 69, 37);
        crown.quadTo(50, 32, 32, 38);
        crown.closeSubpath();
        painter.fillPath(crown, look.hat);

        // The brim, at the SVG's opacity .8.
        QPainterPath brim(QPointF(30, 39));
        brim.quadTo(50, 31, 74, 37);
        brim.lineTo(72, 42);
        brim.quadTo(50, 36, 31, 42);
        brim.closeSubpath();

        painter.setOpacity(0.8);
        painter.fillPath(brim, look.hat);
        painter.setOpacity(1.0);
    }

    painter.setBrush(Qt::NoBrush);

    QPainterPath brows(QPointF(41, 46.5));
    brows.quadTo(44, 44.8, 47, 46.5);
    brows.moveTo(53, 46.5);
    brows.quadTo(56, 44.8, 59, 46.5);
    painter.strokePath(brows, QPen(table.portraitBrow, 1.6, Qt::SolidLine, Qt::RoundCap));

    QPainterPath nose(QPointF(48, 52));
    nose.quadTo(50, 56, 52, 52);
    painter.strokePath(nose, QPen(look.shade, 1.4, Qt::SolidLine, Qt::FlatCap));

    if(look.mustache.isValid())
    {
        QPainterPath mustache(QPointF(41, 58));
        mustache.quadTo(50, 53, 59, 58);
        mustache.quadTo(55, 61, 50, 59);
        mustache.quadTo(45, 61, 41, 58);
        mustache.closeSubpath();
        painter.fillPath(mustache, look.mustache);
    }
    else
    {
        QPainterPath mouth(QPointF(45, 60));
        mouth.quadTo(50, 62.5, 55, 60);
        painter.strokePath(mouth, QPen(table.portraitMouth, 1.5, Qt::SolidLine, Qt::RoundCap));
    }

    if(look.beard.isValid())
    {
        // At the SVG's opacity .35: a shadow of stubble rather than a beard.
        QPainterPath beard(QPointF(36, 54));
        beard.quadTo(38, 70, 50, 70);
        beard.quadTo(62, 70, 64, 54);
        beard.quadTo(60, 64, 50, 64);
        beard.quadTo(40, 64, 36, 54);
        beard.closeSubpath();

        painter.setOpacity(0.35);
        painter.fillPath(beard, look.beard);
        painter.setOpacity(1.0);
    }

    painter.end();

    applyPortraitFilter(image);

    return QPixmap::fromImage(std::move(image));
}
