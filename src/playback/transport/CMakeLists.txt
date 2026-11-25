# Keeps one track fed to one output. Nothing here shapes samples, so the DSP is not among these.

add_library(
    wiola_playback_transport
    STATIC
    decode_loop.cpp
    playback.cpp
    playback_epoch.cpp
    player.cpp
    playhead.cpp
    seek_mailbox.cpp
    seek_operation.cpp
    transport_worker.cpp
    transport_wait.cpp
)

target_compile_features(wiola_playback_transport PUBLIC cxx_std_23)

target_include_directories(
    wiola_playback_transport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback_transport)
endif()

target_link_libraries(
    wiola_playback_transport
    PUBLIC
        wiola::media
        wiola::audio_output
        wiola_playback_buffer
        wiola::playback_api
    PRIVATE Threads::Threads
)
