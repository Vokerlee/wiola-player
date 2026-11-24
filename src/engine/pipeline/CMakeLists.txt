wiola_add_library(
    wiola_engine_pipeline
    SOURCES output_session.cpp playback_pipeline.cpp
)
target_link_libraries(
    wiola_engine_pipeline
    PUBLIC wiola_audio_dsp wiola_engine_transport wiola_playback_buffer
)
