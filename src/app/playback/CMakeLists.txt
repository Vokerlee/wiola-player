if(
    WIOLA_COMPONENT_BUILD
    AND NOT playback_service IN_LIST WIOLA_SELECTED_COMPONENTS
)
    return()
endif()

include(headers)

add_library(wiola_playback_preferences_api INTERFACE)

add_library(
    wiola::playback_preferences_api
    ALIAS wiola_playback_preferences_api
)

target_compile_features(wiola_playback_preferences_api INTERFACE cxx_std_23)

target_link_libraries(
    wiola_playback_preferences_api
    INTERFACE wiola::playback_api
)

wiola_target_headers(
    wiola_playback_preferences_api
    wiola/app/playback
    HEADERS playback_settings.hpp queue_settings_store.hpp volume_scale.hpp
)

add_library(
    wiola_playback_service
    STATIC
    playback_service.cpp
    volume_control.cpp
    equalizer_control.cpp
)

add_library(wiola::playback_service ALIAS wiola_playback_service)

set_target_properties(wiola_playback_service PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(
    wiola_playback_service
    PUBLIC wiola::playback wiola::audio_dsp wiola::playback_preferences_api
)

wiola_target_headers(
    wiola_playback_service
    wiola/app/playback
    HEADERS
        playback_service.hpp
        playback_presentation.hpp
        volume_control.hpp
        equalizer_control.hpp
)

wiola_apply_build_controls(wiola_playback_service)
