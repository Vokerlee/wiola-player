# Keeps one track fed to one output. Nothing here shapes samples, so the DSP is not among these.
find_package(Threads REQUIRED)

wiola_add_library(
    wiola_engine_transport
    SOURCES
        decode_loop.cpp
        playback.cpp
        playback_epoch.cpp
        player.cpp
        playhead.cpp
        seek_mailbox.cpp
        seek_operation.cpp
        transport_worker.cpp
        transport_wait.cpp
    LIBS Threads::Threads
)
target_link_libraries(
    wiola_engine_transport
    PUBLIC wiola::media wiola::audio_output
)
