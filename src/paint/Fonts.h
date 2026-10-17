#ifndef FONTS_H
#define FONTS_H

#include <QStringList>

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

// The fallback chains, in QFont::setFamilies() order, so a missing face
// degrades to the nearest thing rather than to the platform default. Each one
// leads with its bundled family; the rest are section 2.3's, copied from the
// mock-up's CSS.
QStringList logo();
QStringList heading();
QStringList body();

} // namespace fonts

#endif
