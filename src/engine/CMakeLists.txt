add_subdirectory(load)
add_subdirectory(pipeline)
add_subdirectory(queue)

wiola_add_library(
    wiola_engine_session
    SOURCES session.cpp
    LIBS wiola_engine_load wiola_engine_queue
)
target_link_libraries(
    wiola_engine_session
    PUBLIC
        wiola::playback_api
        wiola_audio_dsp
        wiola_engine_pipeline
        wiola_playback_transport
)

wiola_add_module(
    wiola_engine
    LIBS
        wiola_engine_load
        wiola_engine_pipeline
        wiola_engine_queue
        wiola_engine_session
        wiola_playback_transport
)
