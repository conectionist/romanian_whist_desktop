cmake_minimum_required(VERSION 3.21)

project(romanian_whist_desktop
    VERSION 0.1.0
    DESCRIPTION "Qt desktop client for Romanian Whist"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Qt
# ---------------------------------------------------------------------------
# Qt 6 only. The engine is C++20 and every line of
# docs/QT_CLIENT_PLAN_proposal1.md is written against Qt 6 APIs, so a Qt 5
# fallback would only half-support a configuration nothing is tested against.
#
# Svg is linked for QSvgRenderer, not for the qsvg imageformat plugin: the
# deploy step below (see EXCLUDE_PLUGIN_TYPES / --skip-plugin-types) deliberately
# drops iconengines and imageformats, so QPixmap(":/cards/....svg") would come
# back null in an installed build. Rasterizing through QSvgRenderer sidesteps
# that plugin entirely - Svg is a linked library, not a plugin - so the card
# art can stay vector without reopening the plugin trimming above.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets LinguistTools Svg)

# Turns on AUTOMOC and AUTOUIC and the rest of the modern defaults, so they do
# not have to be set one at a time. NOT AUTORCC - see the card art below, where
# that gap is the second of the three silent ways to embed no resources at all.
qt_standard_project_setup(REQUIRES 6.5)

# MSVC compiles one .cpp per cl.exe invocation by default, leaving the rest of
# a multi-core CI runner idle during the build - this is what made CI's Build
# step take 9+ minutes on a 4-core/8-thread machine. /MP lets cl.exe compile
# multiple files from one project in parallel. Set here, before any
# add_subdirectory(), so it also reaches the engine submodule and Catch2 -
# unlike WHIST_DESKTOP_WARNINGS below, this changes nothing about compiler
# output, only how many files compile at once.
if(MSVC)
    add_compile_options(/MP)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
# Everything but main(), so the tests have something to link against - the
# executable is then just an entry point. Copied from romanian_whist_terminal,
# whose CMakeLists.txt splits terminal_core out for exactly this reason.
#
# qt_add_library rather than add_library so the Qt tooling treats it as a Qt
# target: AUTOMOC and AUTOUIC are on globally via qt_standard_project_setup()
# above, and MainWindow needs both - Q_OBJECT for the first, the .ui for the
# second.
qt_add_library(desktop_core STATIC
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui        # AUTOUIC turns this into ui_MainWindow.h

    src/core/CardOrder.h
    src/core/CardOrder.cpp
    src/core/GameSnapshot.h
    src/core/HandLayout.h
    src/core/HandLayout.cpp
    src/core/Snapshot.h
    src/core/Snapshot.cpp

    src/engine/GameAbandoned.h
    src/engine/Prompts.h
    src/engine/QtMoveProvider.h
    src/engine/QtMoveProvider.cpp
    src/engine/GameBridge.h
    src/engine/GameBridge.cpp
    src/engine/GameController.h
    src/engine/GameController.cpp
    src/engine/MetaTypes.h
    src/engine/MetaTypes.cpp

    src/paint/Theme.h
    src/paint/Theme.cpp
    src/paint/CardRenderer.h
    src/paint/CardRenderer.cpp

    src/table/SeatLayout.h
    src/table/SeatLayout.cpp
    src/table/CardItem.h
    src/table/CardItem.cpp
    src/table/SeatItem.h
    src/table/SeatItem.cpp
    src/table/TrickItem.h
    src/table/TrickItem.cpp
    src/table/HandItem.h
    src/table/HandItem.cpp
    src/table/TableScene.h
    src/table/TableScene.cpp
    src/table/TableView.h
    src/table/TableView.cpp

    src/ui/SnapshotLog.h
    src/ui/SnapshotLog.cpp
    src/ui/CardArtWindow.h
    src/ui/CardArtWindow.cpp
    src/ui/InfoStrip.h
    src/ui/InfoStrip.cpp
    src/ui/ScoreboardWidget.h
    src/ui/ScoreboardWidget.cpp

    src/ui/BidStrip.h
    src/ui/BidStrip.cpp
    src/ui/PromptBar.h
    src/ui/PromptBar.cpp

    src/ui/NewGameDialog.h
    src/ui/NewGameDialog.cpp
    src/ui/MainMenuPage.h
    src/ui/MainMenuPage.cpp
    src/ui/PauseOverlay.h
    src/ui/PauseOverlay.cpp
    src/ui/GameOverPanel.h
    src/ui/GameOverPanel.cpp
)

# C++20 because the engine demands it: romanian_whist_engine declares
# target_compile_features(... PUBLIC cxx_std_20), so linking it raises the bar
# through the interface anyway. Said outright here so the tree has one answer.
#
# PUBLIC on both this and the include directory, so main.cpp and the tests see
# the sources without repeating either.
target_compile_features(desktop_core PUBLIC cxx_std_20)

target_include_directories(desktop_core PUBLIC src)

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------
# On OUR targets only. The engine submodule and Catch2 are compiled as they
# come: warnings we cannot fix are warnings we would learn to scroll past, and
# the point of turning these on is that the output stays empty.
#
# NOT -Werror. A compiler upgrade would then break the build over a warning
# nobody chose, on a machine that has no other way to build the client. If this
# ever wants enforcing, CI is where the flag belongs.
#
# /W4 for MSVC is written now rather than when it is next reachable: with the
# Actions quota spent, Windows is currently the platform nothing checks, so the
# flag should already be in place for the first build that runs there.
set(WHIST_DESKTOP_WARNINGS
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall$<SEMICOLON>-Wextra$<SEMICOLON>-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

target_compile_options(desktop_core PRIVATE ${WHIST_DESKTOP_WARNINGS})

target_link_libraries(desktop_core PUBLIC Qt6::Widgets Qt6::Svg)

qt_add_executable(romanian_whist_desktop
    WIN32 MACOSX_BUNDLE
    src/main.cpp
)

target_link_libraries(romanian_whist_desktop PRIVATE desktop_core)

# ---------------------------------------------------------------------------
# Resources
# ---------------------------------------------------------------------------
# The 52 card faces live in resources/images/cards/{png,svg}/ and are listed by
# resources/cards.qrc, which aliases each one to a flat name under the /cards
# prefix. So the source path is resources/images/cards/png/ace_of_spades.png and
# the runtime path is :/cards/ace_of_spades.png (and likewise ace_of_spades.svg
# for the vector set) - moving the files on disk does not move the URLs.
#
# Compiled in explicitly, because there are three silent ways to embed nothing
# here and this call has none of them:
#
#   1. Listing the .png files themselves as target sources - which is what
#      "Add Existing Files" in Qt Creator does - only puts them in the project
#      tree. Nothing embeds them.
#   2. Listing the .qrc as a target source embeds it only if AUTORCC is on, and
#      qt_standard_project_setup() turns on AUTOMOC and AUTOUIC but NOT AUTORCC.
#   3. qt_add_resources(<TARGET> resources/cards.qrc) reads its second argument
#      as a resource NAME, not a file - the target form wants PREFIX and FILES -
#      so it builds an empty resource and generates nothing.
#
# All three configure and build cleanly, run no rcc step, and show up first as
# QPixmap(":/cards/...") returning null at runtime with nothing on stderr.
#
# This is the form that actually consumes a .qrc: an output variable, then the
# .qrc files, with the generated .cpp put into the target by hand. It does not
# depend on AUTORCC. If cards ever stop appearing, look for qrc_cards.cpp in the
# build tree - no such file means nothing was embedded.
qt_add_resources(CARD_RESOURCES resources/cards.qrc)
target_sources(romanian_whist_desktop PRIVATE ${CARD_RESOURCES})

# The TESTS need the same blob, and it is not inherited. CardRenderer lives in
# desktop_core, but resources are not a library property: the generated
# qrc_cards.cpp is a translation unit, and this executable is the only target it
# was put into. So desktop_tests would link a CardRenderer whose art is not
# there, and every card would come back null - the same symptom as all three
# failures above, from a fourth cause.
#
# tests/CMakeLists.txt therefore calls qt_add_resources AGAIN, on the same .qrc,
# for its own target. That looks redundant and is not - see the comment there,
# which records the two ways of sharing this one that were tried and failed.
#
# NOT by moving this into desktop_core, which is the obvious-looking fix. A
# static library's qrc_*.cpp is a translation unit nothing references - the
# resource registers itself from a static initializer - and a linker is entitled
# to drop it. That is silent-embedding failure number four, and it would be
# hidden behind a build that still worked for the executable.

# ---------------------------------------------------------------------------
# Translations
# ---------------------------------------------------------------------------
# Embeds the compiled .qm under :/i18n/, which is where main.cpp looks for it.
# Also defines an update_translations target that refreshes the .ts from source.
qt_add_translations(romanian_whist_desktop
    TS_FILES i18n/romanian_whist_desktop_ro_RO.ts
)

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
# All game logic lives in the engine repository, consumed as a git submodule,
# exactly as romanian_whist_terminal does it:
#
#     git clone --recurse-submodules git@github.com:conectionist/romanian_whist_desktop.git
#
# Cloned without --recurse-submodules? Run:
#
#     git submodule update --init --recursive
#
# A missing engine is fatal rather than a warning: there is no useful build
# without it, and skipping it would only defer the failure to link errors that
# say nothing about submodules.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/RomanianWhistEngine/CMakeLists.txt")
    message(FATAL_ERROR
        "The Romanian Whist engine submodule is missing.\n"
        "Run: git submodule update --init --recursive")
endif()

# The engine's own WHIST_BUILD_TESTS defaults to PROJECT_IS_TOP_LEVEL, which is
# false here, so its test suite and the Catch2 fetch behind it are skipped with
# nothing to pass in.
add_subdirectory(libs/RomanianWhistEngine)

# Engine include directories arrive transitively through the PUBLIC
# target_include_directories on RomanianWhist::engine, so there is no include
# path to add here.
#
# PUBLIC here in turn, so those headers reach main.cpp and the test target
# through desktop_core rather than being listed again on each.
target_link_libraries(desktop_core PUBLIC RomanianWhist::engine)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS romanian_whist_desktop
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------------------------------------------------------------------------
# Deployment
# ---------------------------------------------------------------------------
# Ships only what the application actually loads. The default
# qt_generate_deploy_app_script() is deliberately generous - it deploys every
# plugin type Qt knows about plus the whole of Qt's own translation catalogue,
# which here came to 88 files where about a dozen do the work.
#
# The excess is not arbitrary; it follows a chain worth understanding before
# changing any of this. The executable itself links only Qt6Widgets and
# Qt6Core. Every other library in lib/ is there because some *plugin* needed it:
# Qt6Network arrives only because of the tls and networkinformation plugins,
# Qt6Svg only because of the SVG image format and icon engine, Qt6WaylandClient
# only because of the wayland-* plugins. So the plugin list is the real input -
# restrict it and the libraries follow on their own, because the deploy tool
# computes the closure from what it deployed.
#
# The wayland-* plugins deserve a specific mention, since they are the clearest
# case of shipping something inert: they can only ever be loaded by the Wayland
# *platform* plugin, which the default deployment does not include. On a Wayland
# desktop this application runs through XWayland via the xcb platform plugin,
# which is exactly what it did before this change - dropping them costs nothing.
#
# The translations directory is a separate confusion worth naming: it holds
# *Qt's* own strings (the text on standard dialog buttons), not this
# application's. The Romanian translation is compiled into the binary by
# qt_add_translations() above and is unaffected by NO_TRANSLATIONS. Qt ships no
# Romanian catalogue at all - there is no qt_ro.qm - and Qt's built-in strings
# are already English, so an English-only catalogue would be a no-op too.
#
# Each platform needs saying differently, because the options below are only
# honoured on the code path that reads them:
#
#   Linux   the "generic" deploy tool, which is the only one that reads
#           EXCLUDE_PLUGIN_TYPES and NO_TRANSLATIONS. Passing DEPLOY_TOOL_OPTIONS
#           here would draw a warning, hence the separate branch.
#   Windows windeployqt, which ignores those keywords entirely and has to be
#           told in its own flags. Two of those flags are not about plugins:
#           windeployqt ships D3Dcompiler_47.dll and opengl32sw.dll by default,
#           together 24 MB of a 54 MB tree. The first is for ANGLE, which Qt 6
#           no longer uses at all. The second is Mesa's software OpenGL, a
#           fallback for machines with no usable GL driver - a widgets
#           application paints through the raster engine and never asks for a GL
#           context, so it goes too. If Windows users on bare VMs or over RDP
#           ever report the application failing to start, --no-opengl-sw is the
#           first thing to put back.
#   macOS   macdeployqt, which has no plugin filtering at all. Nothing here can
#           trim it, so it keeps the stock behaviour and stays generous.
#
# Both lists are denylists, and that is a limitation of Qt rather than a choice.
# There is no "deploy only these" form: INCLUDE_PLUGIN_TYPES reads as though it
# were one, but Qt6CoreDeploySupport.cmake ADDS it to the types inferred from the
# linked modules rather than replacing them, so passing it changes nothing here.
# windeployqt likewise offers only --skip-plugin-types. The consequence is that a
# future Qt introducing a new plugin type will deploy it until it is named below,
# so these lists are worth re-reading after a Qt upgrade. The platforms type is
# absent from both deliberately: it is the one that must ship, and Qt deploys it
# separately no matter what these lists say.
#
# Note that dropping every plugin type but platforms also drops the ones that
# affect how the application LOOKS rather than whether it runs: the GTK platform
# theme on Linux and the native style plugin on Windows. That is the intended
# trade - "only what it needs" - but it is the line to revisit first if the
# application ever looks out of place on either desktop.
if(APPLE)
    qt_generate_deploy_app_script(
        TARGET romanian_whist_desktop
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
elseif(WIN32)
    qt_generate_deploy_script(
        TARGET romanian_whist_desktop
        OUTPUT_SCRIPT deploy_script
        CONTENT "
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:romanian_whist_desktop>\"
    GENERATE_QT_CONF
    DEPLOY_TOOL_OPTIONS
        --no-translations
        --skip-plugin-types generic,iconengines,imageformats,networkinformation,styles,tls
        --no-system-d3d-compiler
        --no-opengl-sw
)
")
else()
    qt_generate_deploy_script(
        TARGET romanian_whist_desktop
        OUTPUT_SCRIPT deploy_script
        CONTENT "
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:romanian_whist_desktop>\"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    EXCLUDE_PLUGIN_TYPES
        egldeviceintegrations
        generic
        iconengines
        imageformats
        networkinformation
        platforminputcontexts
        platformthemes
        tls
        wayland-decoration-client
        wayland-graphics-integration-client
        wayland-shell-integration
        xcbglintegrations
)
")
endif()

install(SCRIPT ${deploy_script})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Off when this project is consumed by another, exactly as the engine's
# WHIST_BUILD_TESTS is - so nothing downstream builds or fetches our test
# dependencies. Same shape romanian_whist_terminal uses.
option(WHIST_DESKTOP_BUILD_TESTS "Build the desktop client's test suite" ${PROJECT_IS_TOP_LEVEL})

if(WHIST_DESKTOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
