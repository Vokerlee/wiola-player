add_subdirectory(load)
add_subdirectory(pipeline)
add_subdirectory(queue)
add_subdirectory(transport)

wiola_add_library(
    wiola_engine_session
    SOURCES session.cpp
    LIBS
        wiola_audio
        wiola_codec
        wiola_engine_load
        wiola_engine_pipeline
        wiola_engine_queue
        wiola_engine_transport
)

wiola_add_module(
    wiola_engine
    LIBS
        wiola_engine_load
        wiola_engine_pipeline
        wiola_engine_queue
        wiola_engine_session
        wiola_engine_transport
)
