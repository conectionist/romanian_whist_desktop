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
    SnapshotSequenceTests.cpp
    ThemeTests.cpp
    CardRendererTests.cpp
    SeatLayoutTests.cpp
    TrickItemTests.cpp
    TableSceneTests.cpp
    TestApp.cpp
)

# The card art, compiled into the test binary. CardRendererTests.cpp loads
# ":/cards/..." for real, and the parent CMakeLists.txt attaches its own
# qt_add_resources output to the application target only - resources belong to a
# binary, not to the desktop_core both binaries link.
#
# A SECOND qt_add_resources ON THE SAME .qrc, which looks like duplication and
# is the only form that works. Two tidier-looking arrangements were tried:
#
#   1. Attaching the parent's ${CARD_RESOURCES} to this target - either from
#      here or from the parent after add_subdirectory(). The path resolves and
#      `cmake --build build` succeeds, which is what makes this one dangerous.
#      `cmake --build <dir> --target desktop_tests` then fails outright with
#      "No rule to make target 'qrc_cards.cpp'": the rule that GENERATES that
#      file belongs to the scope that declared it, and this directory's makefile
#      has no copy of it. The by-target build is not an edge case - it is what
#      docs/LOCAL_VERIFICATION.md uses for every sanitizer run.
#   2. Moving the resources into desktop_core, which the parent's Resources
#      block rejects on its own grounds: a static library's qrc_*.cpp is a
#      translation unit nothing references, and a linker may drop it.
#
# So the .qrc is consumed twice, once per binary, and each generated file lives
# in the scope of the target that uses it. The cost is compiling ~30 MB of
# generated source a second time; the alternative is a suite that cannot be run
# under a sanitizer.
qt_add_resources(TEST_CARD_RESOURCES ${CMAKE_SOURCE_DIR}/resources/cards.qrc)
target_sources(desktop_tests PRIVATE ${TEST_CARD_RESOURCES})

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
