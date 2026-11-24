add_subdirectory(pipeline)

wiola_add_test(engine_playlist_test SOURCES playlist_test.cpp LIBS wiola_engine)
target_link_options(engine_playlist_test PRIVATE "LINKER:--wrap=_Znwm")
wiola_add_test(
    engine_fake_decoder_test
    SOURCES fake_decoder_test.cpp
    LIBS wiola::media
)
wiola_add_test(
    engine_loader_test
    SOURCES loader_test.cpp
    LIBS wiola_engine_load
)
wiola_add_test(
    engine_tag_reader_test
    SOURCES tag_reader_test.cpp
    LIBS wiola_engine_load
)
wiola_add_test(
    engine_metadata_coordinator_test
    SOURCES metadata_coordinator_test.cpp
    LIBS wiola_engine_load wiola_engine_queue
)
wiola_add_test(engine_playback_test SOURCES playback_test.cpp LIBS wiola_engine)
wiola_add_test(engine_playhead_test SOURCES playhead_test.cpp LIBS wiola_engine)
wiola_add_test(
    engine_transport_worker_test
    SOURCES transport_worker_test.cpp
    LIBS wiola_engine
)
wiola_add_test(
    engine_player_test
    SOURCES player_test.cpp
    LIBS wiola_codec wiola_engine
    DATA "${PROJECT_SOURCE_DIR}/tests/codec/data"
)
wiola_add_test(
    engine_session_test
    SOURCES session_test.cpp
    LIBS wiola_engine
    DATA "${PROJECT_SOURCE_DIR}/tests/codec/data"
)
