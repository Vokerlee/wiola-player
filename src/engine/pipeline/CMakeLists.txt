wiola_add_library(
    wiola_engine_pipeline
    SOURCES output_session.cpp playback_pipeline.cpp
    LIBS wiola_audio_dsp wiola_engine_transport
)
