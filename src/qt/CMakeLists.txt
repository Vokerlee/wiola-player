if(NOT WIOLA_BUILD_GUI)
    return()
endif()

add_library(wiola_qt_paths INTERFACE)

add_library(wiola::qt_paths ALIAS wiola_qt_paths)

target_compile_features(wiola_qt_paths INTERFACE cxx_std_23)

target_include_directories(
    wiola_qt_paths
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(wiola_qt_paths INTERFACE Qt6::Core)

wiola_add_library(wiola_qt_settings SOURCES settings.cpp LIBS wiola::qt_paths)

target_link_libraries(
    wiola_qt_settings
    PUBLIC Qt6::Core wiola::audio_dsp wiola::playback_preferences_api
)

wiola_add_library(
    wiola_qt_delivery
    SOURCES queued_delivery.cpp
    LIBS Threads::Threads
)

add_library(wiola::qt_delivery ALIAS wiola_qt_delivery)

target_link_libraries(wiola_qt_delivery PUBLIC Qt6::Core)

add_subdirectory(storage)

add_subdirectory(application)

add_subdirectory(projection)

add_subdirectory(widgets)

add_executable(wiola-player main.cpp)

target_link_libraries(
    wiola-player
    PRIVATE
        wiola_core
        wiola_coverage
        wiola::qt_widgets
        wiola_player_application
        wiola_queue_migration
        wiola_sanitizer
        wiola_warnings
)

# Windows opens a console for anything but a GUI binary. Ignored on other systems.
set_target_properties(wiola-player PROPERTIES WIN32_EXECUTABLE ON)

wiola_set_icon(wiola-player "${PROJECT_SOURCE_DIR}/assets/wiola-player.ico")

# A static Qt loads no plugins at runtime, so the platform plugin has to be linked in.
qt_finalize_target(wiola-player)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)

    add_subdirectory(storage/tests)

    add_subdirectory(application/tests)

    add_subdirectory(projection/tests)

    add_subdirectory(widgets/tests)
endif()
