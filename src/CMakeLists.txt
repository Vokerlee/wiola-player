add_subdirectory(foundation)

if(NOT WIOLA_COMPONENT_BUILD OR media IN_LIST WIOLA_SELECTED_COMPONENTS)
    add_subdirectory(media)
endif()

if(NOT WIOLA_COMPONENT_BUILD OR audio IN_LIST WIOLA_SELECTED_COMPONENTS)
    add_subdirectory(audio)
endif()

if(NOT WIOLA_COMPONENT_BUILD OR playback IN_LIST WIOLA_SELECTED_COMPONENTS)
    add_subdirectory(playback)
endif()

if(NOT WIOLA_COMPONENT_BUILD OR library IN_LIST WIOLA_SELECTED_COMPONENTS)
    add_subdirectory(library)
endif()

if(
    NOT WIOLA_COMPONENT_BUILD
    OR library_persistence IN_LIST WIOLA_SELECTED_COMPONENTS
    OR playlist_commands IN_LIST WIOLA_SELECTED_COMPONENTS
)
    add_subdirectory(app)
endif()

if(WIOLA_COMPONENT_BUILD)
    return()
endif()

add_subdirectory(presentation)

add_subdirectory(qt)
