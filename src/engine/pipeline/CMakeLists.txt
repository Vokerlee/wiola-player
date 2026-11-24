wiola_add_library(
    wiola_engine_pipeline
    SOURCES output_session.cpp playback_pipeline.cpp
)
target_link_libraries(
    wiola_engine_pipeline
    PUBLIC wiola::audio_output wiola_playback_transport wiola_playback_buffer
)
