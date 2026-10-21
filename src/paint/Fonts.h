#ifndef FONTS_H
#define FONTS_H

#include <QFont>
#include <QFontMetricsF>

// The table redesign's three typefaces (docs/ui-redesign/ section 2.3), bundled
// rather than hoped for: Cinzel Decorative for the logo, Marcellus for names,
// titles and buttons, Alegreya Sans for everything else. None of them is a
// system font anywhere, so a client that only named them would draw every one
// of them in a fallback on every machine it ships to.
//
// Separate from Theme because a typeface is not a colour or a metric, and
// because registering one needs a function the two binaries can both call -
// the same reason, and the same shape, as engine/MetaTypes.h.
namespace fonts
{

// Adds every .ttf under :/fonts/ to the application's font database. Call it
// once, after the QApplication exists and before the first widget does.
//
// THE TEST SUITE CALLS IT TOO (tests/TestApp.cpp), for MetaTypes.h's reason:
// registered in the application alone, every test would lay text out in a
// fallback face the shipped program never uses.
//
// A file that fails to load is a qWarning naming it, not silence. The likeliest
// cause is fonts.qrc not being compiled into the binary at all - CMakeLists.txt
// lists the three ways that happens with nothing on stderr - and the symptom
// otherwise is text that looks slightly wrong and no clue why.
void registerBundled();

// The fallback chains, so a missing face degrades to the nearest thing rather
// than to the platform default. Each one leads with its bundled family; the
// rest are section 2.3's, copied from the mock-up's CSS.
//
// A QFont RATHER THAN A LIST OF NAMES, because CSS's last step - "serif",
// "sans-serif" - is not a family name to Qt. Fontconfig happens to alias those
// two on Linux; Windows and macOS have no family called either, so as the end
// of a setFamilies() list they matched nothing. Qt's spelling of a generic
// family is the style hint, which only a QFont can carry.
//
// No size and no weight: copy the font and set both on the copy.
QFont logo();
QFont heading();
QFont body();

// A FONT OF A FRACTIONAL PIXEL SIZE, which QFont cannot hold: its pixel size is a
// whole number. The mock-up writes 14.5 and 19.8, so the font is set at the least
// whole multiple of the size - 14.5 at 29, 19.8 at 99 - and drawn scaled down by
// that multiple, `scale`. A whole size comes back as it is, at a scale of 1.
//
// NOT A POINT SIZE, which would be fractional and is the obvious answer: Qt turns
// points into pixels at the device's logical DPI, which is the screen's, so the
// panels would be laid out differently on every screen.
struct Sized
{
    QFont font;
    qreal scale = 1.0;
};

Sized sized(QFont font, qreal pixels);

// Where a line of text puts its baseline in a line box of `height` at `top`: CSS's
// half-leading, which centres the font's ascent-plus-descent in the line box. In
// one place because every panel that sets text the way the mock-up does - the
// logo, the seats, later the HUD - needs the same answer.
inline qreal baselineIn(const QFontMetricsF& metrics, qreal top, qreal height)
{
    return top + (height - (metrics.ascent() + metrics.descent())) / 2.0 + metrics.ascent();
}

} // namespace fonts

#endif
