cmake_minimum_required(VERSION 3.21)

project(romanian_whist_desktop
    VERSION 0.1.0
    DESCRIPTION "Qt desktop client for Romanian Whist"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Qt
# ---------------------------------------------------------------------------
# Qt 6 only. The engine is C++20 and every line of docs/QT_CLIENT_PLAN.md is
# written against Qt 6 APIs, so a Qt 5 fallback would only half-support a
# configuration nothing is tested against.
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets LinguistTools)

# Turns on AUTOMOC / AUTOUIC / AUTORCC and the rest of the modern defaults, so
# they do not have to be set one at a time.
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
qt_add_executable(romanian_whist_desktop
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui        # AUTOUIC turns this into ui_MainWindow.h
)

# C++20 because the engine demands it: romanian_whist_engine declares
# target_compile_features(... PUBLIC cxx_std_20), so linking it raises the bar
# through the interface anyway. Said outright here so the tree has one answer.
target_compile_features(romanian_whist_desktop PRIVATE cxx_std_20)

target_include_directories(romanian_whist_desktop PRIVATE src)

target_link_libraries(romanian_whist_desktop PRIVATE Qt6::Widgets)

# ---------------------------------------------------------------------------
# Resources
# ---------------------------------------------------------------------------
# The 52 card faces live in resources/images/cards/ and are listed by
# resources/cards.qrc, which aliases each one to a flat name under the /cards
# prefix. So the source path is resources/images/cards/ace_of_spades.png and
# the runtime path is :/cards/ace_of_spades.png - moving the files on disk does
# not move the URLs.
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
target_link_libraries(romanian_whist_desktop PRIVATE RomanianWhist::engine)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS romanian_whist_desktop
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET romanian_whist_desktop
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
