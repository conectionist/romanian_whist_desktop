#include "Theme.h"

#include "paint/Avatars.h"
#include "paint/DevicePixels.h"
#include "paint/Gradients.h"

#include <QGradientStops>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QRadialGradient>
#include <QSizeF>
#include <QString>
#include <QSvgRenderer>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <utility>

namespace
{

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

    // Assigned rather than repeated: the black suits ARE the text colour, and a
    // second copy of it is a second thing to keep in step.
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

    // The mock-up's portrait background's outer stop (table.js portrait()), which
    // fills the box under every avatar's art.
    table.portraitEdge = QColor(0x17, 0x10, 0x0A);

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

    // Section 2.8, from page.html's .hcard. .hcard .face: box-shadow -3px 4px 10px
    // rgba(0, 0, 0, 0.45). Raised under the cursor, .hcard.playable:hover .face:
    // 0 0 0 2px gold-bright, 0 0 22px rgba(242, 211, 140, 0.7), 0 14px 22px
    // rgba(0, 0, 0, 0.5).
    table.heldShadow = rgba(0, 0, 0, 0.45);
    table.raisedShadow = rgba(0, 0, 0, 0.5);
    table.raisedGlow = rgba(242, 211, 140, 0.7);

    // .leadpip: background gold-bright, box-shadow 0 0 0 1px rgba(60, 40, 10, 0.5),
    // 0 0 10px rgba(242, 211, 140, 0.8).
    table.leadMarkOutline = rgba(60, 40, 10, 0.5);
    table.leadMarkGlow = rgba(242, 211, 140, 0.8);

    // .bubble: background gold-bright, color #2a1a0a, box-shadow 0 6px 14px
    // rgba(0, 0, 0, 0.5).
    table.bubbleText = QColor(0x2A, 0x1A, 0x0A);
    table.bubbleShadow = rgba(0, 0, 0, 0.5);

    // Sections 2.9 to 2.11, from page.html. .panel.btn:hover: inset 0 0 0 4px
    // rgba(242, 211, 140, 0.6) and 0 0 18px rgba(242, 211, 140, 0.25). The rules:
    // .score th's border-bottom, .score .divider and .trickinfo .prompt's border-top,
    // all 1px rgba(201, 164, 92, 0.25). .score .trump img: 0 4px 12px
    // rgba(0, 0, 0, 0.55).
    table.panelInnerLineHover = rgba(242, 211, 140, 0.6);
    table.buttonGlow = rgba(242, 211, 140, 0.25);
    table.hudRule = rgba(201, 164, 92, 0.25);
    table.trumpShadow = rgba(0, 0, 0, 0.55);

    // Section 2.12, from page.html's .menubar: background linear-gradient(180deg,
    // #2e1c10, #1a1009); border-bottom 1px #806137, which is panelBorder; box-shadow
    // 0 1px 0 rgba(201, 164, 92, 0.22), 0 6px 16px rgba(0, 0, 0, 0.45). An item
    // under the cursor, .menubar .btn:hover: background rgba(201, 164, 92, 0.13).
    table.menuTop = QColor(0x2E, 0x1C, 0x10);
    table.menuBottom = QColor(0x1A, 0x10, 0x09);
    table.menuHover = rgba(201, 164, 92, 0.13);
    table.menuGlint = rgba(201, 164, 92, 0.22);
    table.menuShadow = rgba(0, 0, 0, 0.45);

    // Section 2.13, from page.html's .bidstrip .opt: box-shadow 0 4px 10px
    // rgba(0, 0, 0, 0.45); under the cursor 0 0 0 2px gold-bright, 0 0 16px
    // rgba(242, 211, 140, 0.5); and :disabled background #2a211a, color #6d604d,
    // border-color #4d3d28.
    table.bidShadow = rgba(0, 0, 0, 0.45);
    table.bidGlow = rgba(242, 211, 140, 0.5);
    table.bidDisabledFill = QColor(0x2A, 0x21, 0x1A);
    table.bidDisabledText = QColor(0x6D, 0x60, 0x4D);
    table.bidDisabledBorder = QColor(0x4D, 0x3D, 0x28);

    // Section 2.13, from page.html's .scrim (background rgba(8, 5, 3, 0.62)) and
    // .sheet th / td (border-bottom 1px rgba(201, 164, 92, 0.3) and 0.1).
    table.scrim = rgba(8, 5, 3, 0.62);
    table.sheetHeadRule = rgba(201, 164, 92, 0.3);
    table.sheetRowRule = rgba(201, 164, 92, 0.1);

    // Phase 11, from page.html's .sheet .mini figure.win img: box-shadow 0 0 0 2px
    // gold-bright, 0 0 16px rgba(242, 211, 140, 0.5).
    table.sheetWinGlow = rgba(242, 211, 140, 0.5);

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

// THE TALLEST A PNG AVATAR IS KEPT. The art comes 800 x 912, about 2.9 MB decoded,
// and the box it fills is 100 x 114 at the mock-up's scale: about 228 tall on a 4K
// screen, so 456 leaves twice that. Kept at full size, every face worn stayed 2.9 MB
// for the cache's life (PR #40's review, finding #3); at this size it is about 0.7 MB.
constexpr int maximumArtHeight = 456;

// ONE AVATAR'S ART AS LOADED: a parsed SVG, or a decoded PNG no taller than
// maximumArtHeight. Neither when the file is missing or unreadable, which draws
// the bare box and says so on stderr.
struct AvatarArt
{
    std::shared_ptr<QSvgRenderer> svg;
    QImage image;
};

AvatarArt loadAvatarArt(const QString& path)
{
    AvatarArt art;

    if(path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
    {
        auto renderer = std::make_shared<QSvgRenderer>(path);

        if(renderer->isValid())
            art.svg = std::move(renderer);
    }
    else
    {
        // THROUGH QImageReader, as the textures are (loadTexture() says why): a
        // QPixmap load would file the art in the process-wide QPixmapCache.
        QImageReader reader(path);
        art.image = reader.read();

        // ONCE, here, rather than at every draw: drawCovering() scales it again
        // to the box, from this smaller picture.
        if(art.image.height() > maximumArtHeight)
            art.image = art.image.scaledToHeight(maximumArtHeight, Qt::SmoothTransformation);
    }

    if(!art.svg && art.image.isNull())
        qWarning("Theme: could not load the avatar %s - is avatars.qrc compiled into this binary?",
                 qUtf8Printable(path));

    return art;
}

// The art scaled to cover `box` and cropped to it, centred: CSS's object-fit:
// cover. A PNG is scaled down smoothly first, since the painter's own bilinear
// filter would alias a picture several times the box's size.
void drawCovering(QPainter& painter, const AvatarArt& art, const QRectF& box)
{
    const QSizeF natural = art.svg ? art.svg->viewBoxF().size() : QSizeF(art.image.size());

    if(natural.isEmpty())
        return;

    const qreal scale = std::max(box.width() / natural.width(), box.height() / natural.height());
    const QSizeF drawn = natural * scale;
    const QRectF target(box.center() - QPointF(drawn.width() / 2.0, drawn.height() / 2.0), drawn);

    painter.save();
    painter.setClipRect(box);

    if(art.svg)
    {
        art.svg->render(&painter, target);
    }
    else
    {
        const QImage scaled = art.image.scaled(drawn.toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(target, scaled);
    }

    painter.restore();
}

// WHAT A CACHED PICTURE WAS PAINTED FOR: a size, a ratio and the colours. One
// struct, compared whole by its own operator==, so the day the key gains a field
// it gains it for every cache below at once - which is the drift the note on
// Theme::Cache records happening twice when each cache spelled its key out.
//
// ALL OF ThemeColours, not the colours the painter beneath it reads today. Which
// members a painter depends on is not visible from the cache lookup, and the
// answer changes: a per-colour key was how the avatar cache came to miss the
// accent, which paintAvatar() read then and the plaque colour alone did not
// cover. A copy of the colours rather than a hash of them, so the comparison can
// neither collide nor miss a member.
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
    std::unordered_map<std::string, QPixmap> avatars;

    // EACH AVATAR'S FILE, read once and kept: the source art, not a box's size, as
    // the textures below are, though a PNG is kept no taller than maximumArtHeight.
    // A PNG decoded again for every seat at every settled resize would cost
    // milliseconds a seat.
    std::unordered_map<std::string, AvatarArt> avatarArt;

    // Keyed on nothing. A texture is source art, not a colour and not a size,
    // so the first load is the only one this cache ever needs to make.
    QPixmap wood;
    QPixmap felt;
};

Theme Theme::light()
{
    Theme theme;
    theme.tableSurface = QColor(0x3D, 0x6B, 0x54);
    theme.textPrimary = QColor(0x1C, 0x1F, 0x1D);

    // The dark table in the light theme too - see Theme.h.
    theme.table = tablePalette();

    theme.cache = std::make_shared<Cache>();
    return theme;
}

// The surface changes tone and the caption with it. Nothing here is a recolouring
// of a card, which section 18 is explicit about: a card is white in the dark too.
Theme Theme::dark()
{
    Theme theme;
    theme.tableSurface = QColor(0x23, 0x40, 0x2F);
    theme.textPrimary = QColor(0xE8, 0xE6, 0xE1);
    theme.table = tablePalette();
    theme.cache = std::make_shared<Cache>();
    return theme;
}

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

QPixmap Theme::avatar(const std::string& id, QSize box, qreal dpr) const
{
    if(box.isEmpty() || dpr <= 0.0)
        return QPixmap();

    if(!cache)
        return rasterize(paintAvatar(id, physicalSize(box, dpr)), dpr);

    // A changed ratio discards the set exactly as a changed box does, and for
    // the same reason: the window moved to another screen, so every avatar in
    // the old set is one nothing will ask for again.
    const PictureKey key = keyFor(box, dpr, *this);

    if(cache->avatarKey != key)
    {
        cache->avatars.clear();
        cache->avatarKey = key;
    }

    auto found = cache->avatars.find(id);
    if(found != cache->avatars.end())
        return found->second;

    return cache->avatars.emplace(id, rasterize(paintAvatar(id, physicalSize(box, dpr)), dpr)).first->second;
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

// THE AVATAR'S FILE, drawn to fill the box as CSS's object-fit: cover does -
// scaled until both sides are covered and cropped, centred - over the dark the
// mock-up's vignette fades to, then given page.html's sepia(0.18) contrast(1.05).
// The table mock-up's six are 100 x 114, the box's own shape, so for them the
// crop is a fraction of a pixel.
//
// PHYSICAL PIXELS, as paintBackground() above and for the same reason.
QPixmap Theme::paintAvatar(const std::string& id, QSize physical) const
{
    // An image rather than a pixmap, for the filter's pixel pass at the end.
    QImage image(physical, QImage::Format_ARGB32_Premultiplied);
    image.fill(table.portraitEdge);

    if(const avatars::Avatar* avatar = avatars::find(id))
    {
        const AvatarArt* art = nullptr;
        AvatarArt loaded;

        if(cache)
        {
            auto found = cache->avatarArt.find(id);

            if(found == cache->avatarArt.end())
                found = cache->avatarArt.emplace(id, loadAvatarArt(avatar->path)).first;

            art = &found->second;
        }
        else
        {
            loaded = loadAvatarArt(avatar->path);
            art = &loaded;
        }

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        drawCovering(painter, *art, QRectF(QPointF(0.0, 0.0), QSizeF(physical)));
    }

    applyPortraitFilter(image);

    return QPixmap::fromImage(std::move(image));
}
