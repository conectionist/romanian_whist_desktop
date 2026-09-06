#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "engine/MetaTypes.h"

#include <QGuiApplication>

#include <memory>

// One QGuiApplication for the whole test binary, the offscreen platform it runs
// on, and the metatype registration that goes with it.
//
// WHY THIS EXISTS: a queued connection is delivered by the receiving thread's
// event loop, and there is no event loop - nor any thread affinity worth the
// name - without a QCoreApplication. The tests that cross the thread boundary
// need both. Catch2WithMain owns main(), so a listener is the hook.
//
// WHY QGuiApplication AND NOT QCoreApplication, which is what this was through
// phase A: QPixmap is not constructible without a QGuiApplication - it aborts,
// it does not degrade - and phase B1's CardRenderer returns one. The old comment
// here argued that a GUI application "would need a display that CI does not
// have", which was the right instinct and the wrong conclusion: the offscreen
// platform plugin is exactly the display-free case, it ships inside qtbase on
// all three platforms rather than being one of the plugins the deploy step
// trims, and it needs no X server, no Wayland socket and no xvfb wrapper.
//
// Still NOT QApplication: nothing here constructs a QWidget, and QtWidgets would
// only add a dependency the suite does not use.
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

        // Must outlive every test and be constructed before any QObject that
        // cares about thread affinity. argc is held by value because
        // QGuiApplication takes it by reference and keeps the reference.
        app = std::make_unique<QGuiApplication>(argc, argv);

        registerMetaTypes();
    }

    void testRunEnded(const Catch::TestRunStats&) override
    {
        app.reset();
    }

private:
    int argc = 1;
    char programName[16] = "desktop_tests";
    char* argv[2] = { programName, nullptr };

    std::unique_ptr<QGuiApplication> app;
};
}

CATCH_REGISTER_LISTENER(QtEnvironment)
