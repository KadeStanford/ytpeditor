cmake_minimum_required(VERSION 3.24)

project(YTPEditor VERSION 0.5.0 LANGUAGES CXX)

option(YTP_BUILD_TESTS "Build the YTP Editor test suite" ON)
option(YTP_ENABLE_QT_UI "Build the Qt user interface when Qt is available" ON)
option(YTP_ENABLE_MLT "Build the MLT media adapter when MLT is available" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(ytp_core
    src/core/rational.cpp
    src/core/time_range.cpp
    src/model/id.cpp
    src/model/project.cpp
    src/model/timeline.cpp
    src/model/effects.cpp
    src/timeline/timeline_editor.cpp
    src/timeline/effects_editor.cpp
    src/export/export_settings.cpp
    src/ytp/ytp_toolkit.cpp
    src/ytp/remix_toolkit.cpp
    src/commands/timeline_commands.cpp
    src/commands/command_stack.cpp
    src/commands/library_commands.cpp
)

target_include_directories(ytp_core PUBLIC src)

if(MSVC)
    target_compile_options(ytp_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(ytp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ytp_editor src/app/main.cpp)
target_link_libraries(ytp_editor PRIVATE ytp_core)

# Qt and MLT are optional during the dependency-free foundation stage. Keeping
# the checks here makes a missing workstation dependency explicit at configure
# time without preventing the core model and its tests from being built.
if(YTP_ENABLE_QT_UI)
    find_package(Qt6 6.5 QUIET COMPONENTS Core Concurrent Gui Qml Quick Multimedia Network)
    if(Qt6_FOUND)
        message(STATUS "Qt ${Qt6_VERSION} found; building the QML desktop shell")
        set(CMAKE_AUTOMOC ON)
        qt_policy(SET QTP0001 NEW)
        qt_policy(SET QTP0004 NEW)
        set_target_properties(ytp_editor PROPERTIES AUTOMOC ON)
        target_compile_definitions(ytp_editor PRIVATE YTP_HAS_QT=1)
        target_link_libraries(ytp_editor PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        qt_add_qml_module(ytp_editor
            URI YTPEditor
            VERSION 1.0
            QML_FILES
                qml/Main.qml
                qml/NleTopBar.qml
                qml/StatusStrip.qml
                qml/CommandPalette.qml
                qml/ToastOverlay.qml
                qml/YtpActionButton.qml
                qml/YtpToolIcon.qml
                qml/YtpToolRow.qml
                qml/YtpCategoryHeader.qml
                qml/YtpPackRow.qml
                qml/YtpToolbox.qml
                qml/TimelineFilmstrip.qml
        )

        add_library(ytp_project_io
            src/persistence/project_serializer.cpp
            src/persistence/session_serializer.cpp
            src/media/media_probe.cpp
            src/media/media_cache.cpp
            src/media/media_analysis.cpp
            src/media/native_media_player.cpp
            src/ui/clip_library_model.cpp
            src/ui/media_library_model.cpp
            src/ui/project_controller.cpp
            src/ui/timeline_controller.cpp
            src/export/render_engine.cpp
            src/ui/export_controller.cpp
        )
        target_include_directories(ytp_project_io PUBLIC src)
        target_link_libraries(ytp_project_io PUBLIC ytp_core Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Network)
        if(WIN32)
            target_link_libraries(ytp_project_io PUBLIC mfplay mfplat mfuuid ole32 propsys)
            add_executable(ytp_native_media_validation tools/native_media_validation.cpp)
            target_link_libraries(ytp_native_media_validation PRIVATE ytp_project_io Qt6::Core Qt6::Gui)
        endif()
        target_link_libraries(ytp_editor PRIVATE ytp_project_io)
    else()
        message(STATUS "Qt 6 not found; building the foundation console shell")
    endif()
endif()

if(YTP_ENABLE_MLT)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MLT QUIET IMPORTED_TARGET mlt-framework-7)
        pkg_check_modules(MLTPP QUIET IMPORTED_TARGET mlt++-7)
    endif()
    if(MLT_FOUND AND MLTPP_FOUND)
        message(STATUS "MLT found; building the media-engine spike")
        add_executable(ytp_media_spike tools/media_engine_spike.cpp)
        target_link_libraries(ytp_media_spike PRIVATE PkgConfig::MLTPP)
    else()
        message(STATUS "MLT not found; media playback/render spike remains gated")
    endif()
endif()

if(YTP_BUILD_TESTS)
    enable_testing()
    add_executable(ytp_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(ytp_core_tests PRIVATE ytp_core)
    add_test(NAME ytp_core_tests COMMAND ytp_core_tests)

    if(MINGW)
        set_tests_properties(ytp_core_tests PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
        )
    endif()

    if(Qt6_FOUND)
        add_executable(ytp_project_io_tests tests/project_io_tests.cpp)
        target_link_libraries(ytp_project_io_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_project_io_tests COMMAND ytp_project_io_tests)
        add_executable(ytp_controller_integration_tests tests/controller_integration_tests.cpp)
        target_link_libraries(ytp_controller_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_controller_integration_tests COMMAND ytp_controller_integration_tests)
        add_executable(ytp_playback_integration_tests tests/playback_integration_tests.cpp)
        target_link_libraries(ytp_playback_integration_tests PRIVATE Qt6::Core Qt6::Multimedia)
        add_test(NAME ytp_playback_integration_tests COMMAND ytp_playback_integration_tests)
        add_executable(ytp_timeline_integration_tests tests/timeline_integration_tests.cpp)
        target_link_libraries(ytp_timeline_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_timeline_integration_tests COMMAND ytp_timeline_integration_tests)
        add_executable(ytp_controller_surface_tests tests/controller_surface_tests.cpp)
        target_link_libraries(ytp_controller_surface_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_controller_surface_tests COMMAND ytp_controller_surface_tests)
        add_executable(ytp_effects_integration_tests tests/effects_integration_tests.cpp)
        target_link_libraries(ytp_effects_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_effects_integration_tests COMMAND ytp_effects_integration_tests)
        add_executable(ytp_export_integration_tests tests/export_integration_tests.cpp)
        target_link_libraries(ytp_export_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_export_integration_tests COMMAND ytp_export_integration_tests)
        add_executable(ytp_alpha_dogfood_tests tests/alpha_dogfood_tests.cpp)
        target_link_libraries(ytp_alpha_dogfood_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_alpha_dogfood_tests COMMAND ytp_alpha_dogfood_tests)
        add_executable(ytp_toolkit_integration_tests tests/ytp_toolkit_integration_tests.cpp)
        target_link_libraries(ytp_toolkit_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_toolkit_integration_tests COMMAND ytp_toolkit_integration_tests)
        add_executable(ytp_milestone6_integration_tests tests/milestone6_integration_tests.cpp)
        target_link_libraries(ytp_milestone6_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_milestone6_integration_tests COMMAND ytp_milestone6_integration_tests)
        add_executable(ytp_milestone7_integration_tests tests/milestone7_integration_tests.cpp)
        target_link_libraries(ytp_milestone7_integration_tests PRIVATE ytp_project_io Qt6::Core)
        add_test(NAME ytp_milestone7_integration_tests COMMAND ytp_milestone7_integration_tests)
        add_executable(ytp_qml_interaction_tests tests/qml_interaction_tests.cpp)
        target_compile_definitions(ytp_qml_interaction_tests PRIVATE YTP_QML_MAIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
        target_link_libraries(ytp_qml_interaction_tests PRIVATE ytp_project_io Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME ytp_qml_interaction_tests COMMAND ytp_qml_interaction_tests)
        if(WIN32)
            add_test(NAME ytp_controller_api_coverage COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-controller-test-coverage.ps1" -Root "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        add_test(NAME ytp_qml_smoke COMMAND ytp_editor --smoke-test)
        add_test(NAME ytp_milestone8_standard_ui COMMAND ytp_editor --show-toolkit --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/milestone8-standard.png")
        add_test(NAME ytp_milestone8_remix_ui COMMAND ytp_editor --show-remix --show-dual --window-size 1920 1080 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/milestone8-remix.png")
        add_test(NAME ytp_paged_edit_ui COMMAND ytp_editor --inspector-tab 1 --inspector-section 2 --window-size 1920 1080 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/paged-edit.png")
        add_test(NAME ytp_paged_randomizer_ui COMMAND ytp_editor --inspector-tab 3 --inspector-section 8 --window-size 1920 1080 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/paged-randomizer.png")
        add_test(NAME ytp_paged_finish_ui COMMAND ytp_editor --inspector-tab 4 --inspector-section 2 --window-size 1920 1080 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/paged-finish.png")
        add_test(NAME ytp_paged_remix_ui COMMAND ytp_editor --inspector-tab 5 --inspector-section 3 --window-size 1920 1080 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/paged-remix.png")
        add_test(NAME ytp_source_wheel_ui COMMAND ytp_editor --inspector-tab 0 --test-inspector-wheel --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/source-wheel.png")
        add_test(NAME ytp_edit_wheel_ui COMMAND ytp_editor --inspector-tab 1 --inspector-section 2 --test-inspector-wheel --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/edit-wheel.png")
        add_test(NAME ytp_mixer_wheel_ui COMMAND ytp_editor --inspector-tab 2 --test-inspector-wheel --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/mixer-wheel.png")
        add_test(NAME ytp_inspector_wheel_ui COMMAND ytp_editor --inspector-tab 3 --inspector-section 8 --test-inspector-wheel --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/inspector-wheel.png")
        add_test(NAME ytp_finish_wheel_ui COMMAND ytp_editor --inspector-tab 4 --inspector-section 2 --test-inspector-wheel --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/finish-wheel.png")
        add_test(NAME ytp_remix_wheel_ui COMMAND ytp_editor --inspector-tab 5 --inspector-section 3 --test-inspector-wheel --window-size 1024 640 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/remix-wheel.png")
        set_tests_properties(ytp_qml_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 10
        )
        set_tests_properties(ytp_qml_interaction_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 120
        )
        set_tests_properties(ytp_milestone8_standard_ui ytp_milestone8_remix_ui ytp_paged_edit_ui ytp_paged_randomizer_ui ytp_paged_finish_ui ytp_paged_remix_ui ytp_source_wheel_ui ytp_edit_wheel_ui ytp_mixer_wheel_ui ytp_inspector_wheel_ui ytp_finish_wheel_ui ytp_remix_wheel_ui PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 15
        )
        if(MINGW)
            set_tests_properties(ytp_project_io_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
            set_tests_properties(ytp_controller_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 60
            )
            set_tests_properties(ytp_playback_integration_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 60
            )
            set_tests_properties(ytp_timeline_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
            set_tests_properties(ytp_controller_surface_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
            set_tests_properties(ytp_effects_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 120
            )
            set_tests_properties(ytp_export_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 180
            )
            set_tests_properties(ytp_alpha_dogfood_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
            set_tests_properties(ytp_toolkit_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 60
            )
            set_tests_properties(ytp_milestone6_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 60
            )
            set_tests_properties(ytp_milestone7_integration_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
                TIMEOUT 180
            )
            set_tests_properties(ytp_qml_smoke PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
            set_tests_properties(ytp_qml_interaction_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
            set_tests_properties(ytp_milestone8_standard_ui ytp_milestone8_remix_ui ytp_paged_edit_ui ytp_paged_randomizer_ui ytp_paged_finish_ui ytp_paged_remix_ui ytp_source_wheel_ui ytp_edit_wheel_ui ytp_mixer_wheel_ui ytp_inspector_wheel_ui ytp_finish_wheel_ui ytp_remix_wheel_ui PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/msys64/ucrt64/bin"
            )
        endif()
    endif()
endif()
