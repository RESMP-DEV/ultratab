# Ultra Tab: a second app beside the lapis window that deals the agents that
# need you as a deck of cards. It reuses the session transport and launch
# identity and reads only what lapis publishes (docs/ultratab.md).
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Qml Quick Network Svg)
set(CMAKE_AUTOMOC ON)
if(APPLE)
    enable_language(OBJCXX)
endif()

add_library(lapis_ultratab_core STATIC
    src/published.cpp src/published.hpp
    src/cards.cpp src/cards.hpp
    src/settings.cpp src/settings.hpp
    src/deck.cpp src/deck.hpp
    src/session_sender.cpp src/session_sender.hpp
    src/composer.cpp src/composer.hpp
    src/hotkey.cpp src/hotkey.hpp
    src/overlay_view.cpp src/overlay_view.hpp
    src/platform_overlay.hpp)
target_include_directories(lapis_ultratab_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# The Tab order is shared with the desktop through one header-only file.
target_include_directories(lapis_ultratab_core PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src")
target_link_libraries(lapis_ultratab_core PRIVATE lapis_project_options
    PUBLIC lapis_local_protocol lapis_session_launch Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
        Qt6::Network Qt6::Svg)
qt_add_resources(lapis_ultratab_core ultratab_qml PREFIX "/ultratab"
                 FILES qml/Overlay.qml qml/HarnessTile.qml)
# The card composer and the next-prompt helper it imports, written beside
# each other into runtime/ultratab_compose/ at launch (src/composer.hpp).
set(ULTRATAB_NEXT_PROMPT "${PROJECT_SOURCE_DIR}/apps/desktop/src/next_prompt.py")
set_source_files_properties(compose/compose.py PROPERTIES QT_RESOURCE_ALIAS compose.py)
set_source_files_properties("${ULTRATAB_NEXT_PROMPT}" PROPERTIES QT_RESOURCE_ALIAS next_prompt.py)
qt_add_resources(lapis_ultratab_core ultratab_compose PREFIX "/ultratab/compose"
    FILES compose/compose.py "${ULTRATAB_NEXT_PROMPT}")

# A standalone app: "Ultra Tab.app", no Dock icon (LSUIElement), its own
# bundle identity. scripts/package_ultratab.py adds the icon and signs it.
qt_add_executable(lapis_ultratab MACOSX_BUNDLE src/main.cpp)
if(APPLE)
    target_sources(lapis_ultratab PRIVATE src/platform_overlay_mac.mm)
    target_link_libraries(lapis_ultratab PRIVATE "-framework AppKit" "-framework Carbon"
        "-framework ServiceManagement")
endif()
target_link_libraries(lapis_ultratab PRIVATE lapis_project_options lapis_ultratab_core)
set_target_properties(lapis_ultratab PROPERTIES
    OUTPUT_NAME "Ultra Tab"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "Ultra Tab"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.ultratab.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Ultra Tab contributors. MIT License.")

if(BUILD_TESTING)
    add_executable(lapis_ultratab_tests tests/ultratab_test.cpp)
    target_link_libraries(lapis_ultratab_tests PRIVATE lapis_project_options lapis_ultratab_core
        lapis_terminal)
    add_test(NAME ultratab COMMAND lapis_ultratab_tests)
    set_tests_properties(ultratab PROPERTIES TIMEOUT 30)

    # When cards are composed, against a fake helper; no model is called.
    add_executable(lapis_ultratab_composer_tests tests/composer_test.cpp)
    target_link_libraries(lapis_ultratab_composer_tests PRIVATE lapis_project_options
        lapis_ultratab_core)
    add_test(NAME ultratab-composer COMMAND lapis_ultratab_composer_tests)
    set_tests_properties(ultratab-composer PROPERTIES TIMEOUT 30)

    # Loads the overlay offscreen with a fixture and saves a capture to look at.
    add_executable(lapis_ultratab_overlay_tests tests/overlay_test.cpp)
    target_link_libraries(lapis_ultratab_overlay_tests PRIVATE lapis_project_options
        lapis_ultratab_core)
    target_compile_definitions(lapis_ultratab_overlay_tests PRIVATE
        ULTRATAB_CAPTURE_DIR="${PROJECT_SOURCE_DIR}/build/reports/ultratab")
    add_test(NAME ultratab-overlay COMMAND lapis_ultratab_overlay_tests)
    set_tests_properties(ultratab-overlay PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
