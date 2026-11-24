add_subdirectory(pipeline)

wiola_add_library(wiola_engine_session SOURCES session.cpp session_impl.cpp)
target_link_libraries(
    wiola_engine_session
    PUBLIC wiola::playback_api
    PRIVATE
        wiola_engine_pipeline
        wiola_playback_transport
        wiola_playback_loading
        wiola_playback_metadata
        wiola_playback_queue
        wiola_audio_dsp
)

wiola_add_module(
    wiola_engine
    LIBS
        wiola_playback_metadata
        wiola_engine_pipeline
        wiola_playback_queue
        wiola_engine_session
        wiola_playback_transport
        wiola_playback_loading
)
