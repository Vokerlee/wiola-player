include(GoogleTest)

if(TARGET wiola_library_persistence)
    add_executable(
        app_library_persistence_test
        library/persistence/admission_test.cpp
        library/persistence/snapshot_test.cpp
        library/persistence/notification_test.cpp
        library/persistence/lifecycle_test.cpp
    )

    target_link_libraries(
        app_library_persistence_test
        PRIVATE
            wiola::library_persistence
            wiola_test_allocation_failure
            GTest::gtest_main
            wiola_sanitizer
            wiola_coverage
            wiola_warnings
    )

    gtest_discover_tests(app_library_persistence_test)

    add_executable(
        library_persistence_consumer_test
        "${CMAKE_CURRENT_SOURCE_DIR}/library/persistence_consumer.cpp"
    )

    target_link_libraries(
        library_persistence_consumer_test
        PRIVATE wiola::library_persistence
    )

    wiola_apply_build_controls(library_persistence_consumer_test)

    add_test(
        NAME LibraryPersistence.Consumer
        COMMAND library_persistence_consumer_test
    )
endif()

if(TARGET wiola_saved_playlists)
    foreach(
        test
        IN
        ITEMS
            playlist_name
            playlist_commands
            playlist_destination
            playlist_transfer
    )
        add_executable(app_${test}_test library/${test}_test.cpp)

        target_link_libraries(
            app_${test}_test
            PRIVATE wiola::playlist_commands GTest::gtest_main
        )

        wiola_apply_build_controls(app_${test}_test)

        gtest_discover_tests(app_${test}_test)
    endforeach()

    target_link_libraries(
        app_playlist_commands_test
        PRIVATE wiola_test_allocation_failure
    )

    target_link_libraries(
        app_playlist_destination_test
        PRIVATE wiola_test_allocation_failure
    )
endif()

if(TARGET wiola_metadata_edits)
    foreach(
        test
        IN
        ITEMS
            metadata_conflicts
            metadata_edit_service
            metadata_edit_session
            metadata_merge
    )
        add_executable(app_${test}_test metadata/${test}_test.cpp)

        target_link_libraries(
            app_${test}_test
            PRIVATE wiola::metadata_edits GTest::gtest_main
        )

        wiola_apply_build_controls(app_${test}_test)

        gtest_discover_tests(app_${test}_test)
    endforeach()

    target_link_libraries(
        app_metadata_edit_service_test
        PRIVATE wiola_test_allocation_failure
    )
endif()

if(TARGET wiola_workspace)
    add_executable(
        app_workspace_policy_test
        workspace/workspace_policy_test.cpp
    )

    target_link_libraries(
        app_workspace_policy_test
        PRIVATE wiola::workspace GTest::gtest_main
    )

    wiola_apply_build_controls(app_workspace_policy_test)

    gtest_discover_tests(app_workspace_policy_test)
endif()

if(TARGET wiola_playback_service)
    foreach(test IN ITEMS playback_service playback_preferences)
        add_executable(app_${test}_test playback/${test}_test.cpp)

        target_link_libraries(
            app_${test}_test
            PRIVATE wiola::playback_service GTest::gtest_main
        )

        target_include_directories(
            app_${test}_test
            PRIVATE "${PROJECT_SOURCE_DIR}/tests"
        )

        wiola_apply_build_controls(app_${test}_test)

        gtest_discover_tests(app_${test}_test)
    endforeach()
endif()

if(WIOLA_COMPONENT_BUILD)
    return()
endif()

wiola_add_test(
    app_background_metadata_integration_test
    SOURCES metadata/background_metadata_integration_test.cpp
    LIBS wiola_app
)

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
    LIBS wiola_app wiola_codec wiola_test_allocation_failure
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)

wiola_add_test(
    app_media_replacement_test
    SOURCES metadata/media_replacement_test.cpp
    LIBS wiola_app wiola_codec
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)

target_link_libraries(
    app_media_replacement_test
    PRIVATE wiola::file_fingerprint wiola::local_media
)

wiola_add_test(
    app_workspace_reconciliation_test
    SOURCES workspace/workspace_reconciliation_test.cpp
    LIBS wiola_app
)

target_link_libraries(
    app_workspace_reconciliation_test
    PRIVATE wiola::file_fingerprint
)

wiola_add_test(
    app_workspace_scanner_test
    SOURCES workspace/workspace_scanner_test.cpp
    LIBS wiola_app
)

target_link_libraries(
    app_workspace_scanner_test
    PRIVATE wiola::file_fingerprint
)

wiola_add_test(
    app_queue_settings_test
    SOURCES playback/queue_settings_test.cpp
    LIBS wiola_app
)

target_link_libraries(app_queue_settings_test PRIVATE wiola::local_media)

wiola_add_test(
    app_playback_queue_test
    SOURCES playback/playback_queue_test.cpp
    LIBS wiola_app wiola_codec
)

target_link_libraries(app_playback_queue_test PRIVATE wiola::local_media)

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
