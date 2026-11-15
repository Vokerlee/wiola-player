add_subdirectory(app)
add_subdirectory(audio)
if(WIOLA_BUILD_CLI)
    add_subdirectory(cli)
endif()
add_subdirectory(codec)
add_subdirectory(engine)
if(WIOLA_BUILD_GUI)
    add_subdirectory(gui)
endif()
add_subdirectory(media)

if(NOT WIOLA_BUILD_GUI)
    return()
endif()

add_executable(wiola-player main.cpp)
target_link_libraries(
    wiola-player
    PRIVATE
        wiola_cli
        wiola_core
        wiola_coverage
        wiola_gui
        wiola_sanitizer
        wiola_warnings
)

# Windows opens a console for anything but a GUI binary. Ignored on other systems.
set_target_properties(wiola-player PROPERTIES WIN32_EXECUTABLE ON)

wiola_set_icon(wiola-player "${PROJECT_SOURCE_DIR}/assets/wiola-player.ico")

# A static Qt loads no plugins at runtime, so the platform plugin has to be linked in.
qt_finalize_target(wiola-player)
