# Catch2 v3.16.0 - the same tag the engine's own suite pins, so both resolve to
# one target rather than fetching two copies.
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(desktop_tests
    HandLayoutTests.cpp
    SnapshotTests.cpp
    QtMoveProviderTests.cpp
    GameBridgeTests.cpp
    GameControllerTests.cpp
    SnapshotLogTests.cpp
    TestApp.cpp
)

target_link_libraries(desktop_tests PRIVATE
    desktop_core
    Catch2::Catch2WithMain
)

target_include_directories(desktop_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# The same warnings the client is built with, set in the parent CMakeLists.txt.
# The test suite is where a signature gets used, so it catches a different set
# of mistakes than compiling the library alone does.
#
# Note C++20 is not requested here: it arrives through desktop_core's PUBLIC
# cxx_std_20, and stating it twice would let the two drift.
target_compile_options(desktop_tests PRIVATE ${WHIST_DESKTOP_WARNINGS})

# Cards are compared by value here as they are in the engine's suite, and half
# of those comparisons are on an optional. Catch2 renders those "{?}" whatever
# StringMaker specializations are in scope unless this is defined - and it has
# to be seen before catch_tostring.hpp, which catch_test_macros.hpp already
# pulls in, so a #define in a header of ours cannot win against include order.
target_compile_definitions(desktop_tests PRIVATE
    CATCH_CONFIG_ENABLE_OPTIONAL_STRINGMAKER
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(desktop_tests)
