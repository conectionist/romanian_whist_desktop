#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "engine/MetaTypes.h"

#include <QCoreApplication>

#include <memory>

// One QCoreApplication for the whole test binary, and the metatype registration
// that goes with it.
//
// WHY THIS EXISTS: a queued connection is delivered by the receiving thread's
// event loop, and there is no event loop - nor any thread affinity worth the
// name - without a QCoreApplication. The tests that cross the thread boundary
// need both. Catch2WithMain owns main(), so a listener is the hook.
//
// QCoreApplication and NOT QApplication: nothing here opens a window, and a
// QApplication would need a display that CI does not have. Choosing the smaller
// one is what keeps the suite running on three headless runners without
// QT_QPA_PLATFORM=offscreen.
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
        // Must outlive every test and be constructed before any QObject that
        // cares about thread affinity. argc is held by value because
        // QCoreApplication takes it by reference and keeps the reference.
        app = std::make_unique<QCoreApplication>(argc, argv);

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

    std::unique_ptr<QCoreApplication> app;
};
}

CATCH_REGISTER_LISTENER(QtEnvironment)
