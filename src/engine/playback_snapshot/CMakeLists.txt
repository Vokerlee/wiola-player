add_library(wiola_engine_playback_snapshot INTERFACE)
add_library(
    wiola::engine_playback_snapshot
    ALIAS wiola_engine_playback_snapshot
)
target_link_libraries(wiola_engine_playback_snapshot INTERFACE wiola_core)
