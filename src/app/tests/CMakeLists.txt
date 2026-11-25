include(GoogleTest)
add_executable(
    app_library_persistence_test
    library/library_persistence_test.cpp
)
target_link_libraries(
    app_library_persistence_test
    PRIVATE
        wiola::library_persistence
        GTest::gtest_main
        wiola_sanitizer
        wiola_coverage
        wiola_warnings
)
gtest_discover_tests(app_library_persistence_test)
wiola_add_public_consumer_test(
    LibraryPersistence.InstalledConsumer
    PACKAGE wiola_library_persistence
    TARGET wiola::library_persistence
    CONSUMER "${CMAKE_CURRENT_SOURCE_DIR}/library/persistence_consumer.cpp"
    COMPONENTS
        wiola_quantities
        wiola_media
        wiola_library
        wiola_library_persistence
)
if(WIOLA_COMPONENT_BUILD)
    return()
endif()

wiola_add_test(
    app_metadata_batch_edit_test
    SOURCES metadata/metadata_batch_edit_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_custom_tag_commands_test
    SOURCES metadata/custom_tag_commands_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_background_metadata_writes_test
    SOURCES metadata/background_metadata_writes_test.cpp
    LIBS wiola_app wiola_codec
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)
wiola_add_test(
    app_metadata_conflicts_test
    SOURCES metadata/metadata_conflicts_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_media_replacement_test
    SOURCES metadata/media_replacement_test.cpp
    LIBS wiola_app wiola_codec
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)
wiola_add_test(
    app_metadata_edit_service_test
    SOURCES metadata/metadata_edit_service_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_metadata_merge_test
    SOURCES metadata/metadata_merge_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_workspace_reconciliation_test
    SOURCES workspace/workspace_reconciliation_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_workspace_scanner_test
    SOURCES workspace/workspace_scanner_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_playlist_transfer_test
    SOURCES playback/playlist_transfer_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_active_playlist_test
    SOURCES playback/active_playlist_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_playback_queue_test
    SOURCES playback/playback_queue_test.cpp
    LIBS wiola_app wiola_codec
)
wiola_add_test(
    app_playback_service_test
    SOURCES playback/playback_service_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_metadata_single_edit_test
    SOURCES metadata/metadata_single_edit_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_search_index_test
    SOURCES library/search_index_test.cpp
    LIBS wiola_app wiola_test_allocation_failure
)

wiola_add_test(
    app_location_index_test
    SOURCES library/location_index_test.cpp
    LIBS wiola_app wiola_test_allocation_failure
)

wiola_add_test(
    app_tag_key_catalog_test
    SOURCES library/tag_key_catalog_test.cpp
    LIBS wiola_app wiola_test_allocation_failure
)

wiola_add_test(
    app_playback_presentation_test
    SOURCES playback/playback_presentation_test.cpp
    LIBS wiola_app
)

wiola_add_test(
    app_artwork_store_test
    SOURCES library/artwork_store_test.cpp
    LIBS wiola_app
)

wiola_add_test(
    app_local_identity_test
    SOURCES library/local_identity_test.cpp
    LIBS wiola::local_identity
)
