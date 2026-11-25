# Canonical portable component names and their currently required closure.
set(WIOLA_FOUNDATION_COMPONENTS borrowed_reference quantities)
set(WIOLA_PORTABLE_COMPONENTS
    ${WIOLA_FOUNDATION_COMPONENTS}
    media
    audio
    playback
    library
)
set(WIOLA_COMPONENTS
    ""
    CACHE STRING
    "Build only selected portable components; empty builds the player"
)
set_property(
    CACHE WIOLA_COMPONENTS
    PROPERTY STRINGS ${WIOLA_PORTABLE_COMPONENTS}
)
set(WIOLA_SELECTED_COMPONENTS ${WIOLA_COMPONENTS})
set(WIOLA_COMPONENT_BUILD OFF)
if(NOT "${WIOLA_COMPONENTS}" STREQUAL "")
    set(WIOLA_COMPONENT_BUILD ON)
    foreach(component IN LISTS WIOLA_COMPONENTS)
        if(NOT component IN_LIST WIOLA_PORTABLE_COMPONENTS)
            message(FATAL_ERROR "Unknown portable component: ${component}")
        endif()
    endforeach()
    if(playback IN_LIST WIOLA_SELECTED_COMPONENTS)
        list(APPEND WIOLA_SELECTED_COMPONENTS audio)
    endif()
    if(audio IN_LIST WIOLA_SELECTED_COMPONENTS)
        list(APPEND WIOLA_SELECTED_COMPONENTS borrowed_reference media)
    endif()
    if(library IN_LIST WIOLA_SELECTED_COMPONENTS)
        list(APPEND WIOLA_SELECTED_COMPONENTS media)
    endif()
    if(media IN_LIST WIOLA_SELECTED_COMPONENTS)
        list(APPEND WIOLA_SELECTED_COMPONENTS quantities)
    endif()
    list(REMOVE_DUPLICATES WIOLA_SELECTED_COMPONENTS)
    set(WIOLA_BUILD_GUI OFF)
    message(STATUS "Portable components: ${WIOLA_SELECTED_COMPONENTS}")
endif()
