#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "engine/MetaTypes.h"
#include "paint/Fonts.h"

#include <QApplication>

#include <memory>

// One QApplication for the whole test binary, the offscreen platform it runs
// on, and the metatype registration that goes with it.
//
// WHY THIS EXISTS: a queued connection is delivered by the receiving thread's
// event loop, and there is no event loop - nor any thread affinity worth the
// name - without a QCoreApplication. The tests that cross the thread boundary
// need both. Catch2WithMain owns main(), so a listener is the hook.
//
// THE TYPE HAS BEEN WIDENED TWICE, EACH TIME BY SOMETHING THAT ABORTS RATHER
// THAN DEGRADES - which is the pattern worth knowing before writing a test that
// touches a new part of Qt here:
//
//   - QCoreApplication through phase A, until B1's CardRenderer returned a
//     QPixmap. QPixmap is not constructible without a QGuiApplication. The
//     comment that stood here argued a GUI application "would need a display
//     that CI does not have", which was the right instinct and the wrong
//     conclusion: the offscreen platform plugin is exactly the display-free
//     case, it ships inside qtbase on all three platforms rather than being one
//     of the plugins the deploy step trims, and it needs no X server, no
//     Wayland socket and no xvfb wrapper.
//
//   - QGuiApplication until B2a's TableSceneTests constructed a QGraphicsScene.
//     QtWidgets reads QApplication's style and its widget-level state during
//     that constructor, and with only a QGuiApplication it does not warn or
//     return an empty scene - it SEGFAULTS inside libQt6Widgets, from a stack
//     that names nothing but the scene's own constructor.
//
// So the rule the old comment stated - "nothing here constructs a QWidget, and
// QtWidgets would only add a dependency the suite does not use" - was already
// half wrong when it was written: the dependency arrives with the first
// QtWidgets CLASS, and QGraphicsScene is one without being a widget. QtWidgets
// is linked either way, through desktop_core, so this costs the suite nothing it
// was not already paying.
//
// The platform is set here rather than in tests/CMakeLists.txt's test properties
// because the suite is run BOTH ways: ctest, and the bare ./tests/desktop_tests
// that docs/LOCAL_VERIFICATION.md invokes under the sanitizers. A ctest ENVIRONMENT
// property covers the first and silently misses the second, which is the run that
// matters most. Set only when the variable is empty, so `QT_QPA_PLATFORM=xcb` on
// the command line still wins for anyone debugging a rendering difference against
// a real screen.
//
// The registration is the same call main() makes. Doing it here rather than
// leaving it to the application is the point of MetaTypes.h existing at all:
// registered in one binary and not the other, the tests would exercise a
// configuration the shipped program never runs.
namespace
{
class QtEnvironment : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override
    {
        // BEFORE the application is constructed, which is the only point at
        // which the platform plugin is chosen. Setting it afterwards does
        // nothing at all and does not complain.
        if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");

        // WINDOWS ONLY, and for the same reason as the block above: this has to
        // land before QApplication exists. The offscreen platform's font
        // database is FreeType-based on every OS, but only Linux and macOS point
        // it at the system's installed fonts automatically, through fontconfig
        // and CoreText respectively - Windows has neither. Without this,
        // QFontDatabase::families() comes back EMPTY on Windows: every QFont
        // resolves to nothing, and QFont::setBold() becomes a no-op because
        // there is no distinct bold face to switch to. That is not a corner
        // case - ScoreboardTests.cpp's "sized for the weight it draws in" is a
        // direct, deterministic failure of it, discovered the first time this
        // suite ever ran against MSVC.
#ifdef Q_OS_WIN
        if(qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
            qputenv("QT_QPA_FONTDIR", qgetenv("SystemRoot") + "\\Fonts");
#endif

        // Must outlive every test and be constructed before any QObject that
        // cares about thread affinity. argc is held by value because
        // QApplication takes it by reference and keeps the reference.
        app = std::make_unique<QApplication>(argc, argv);

        registerMetaTypes();

        // The same call main() makes, for the same reason as the line above:
        // without it every test lays text out in a face the program never uses.
        fonts::registerBundled();
    }

    void testRunEnded(const Catch::TestRunStats&) override
    {
        app.reset();
    }

private:
    int argc = 1;
    char programName[16] = "desktop_tests";
    char* argv[2] = { programName, nullptr };

    std::unique_ptr<QApplication> app;
};
}

CATCH_REGISTER_LISTENER(QtEnvironment)
