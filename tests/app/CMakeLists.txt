wiola_add_test(
    app_bulk_metadata_commands_test
    SOURCES bulk_metadata_commands_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_custom_tag_commands_test
    SOURCES custom_tag_commands_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_media_revision_publisher_test
    SOURCES media_revision_publisher_test.cpp
    LIBS wiola_media_revisions
)
wiola_add_test(
    app_metadata_merge_test
    SOURCES metadata_merge_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_workspace_watcher_test
    SOURCES workspace_watcher_test.cpp
    LIBS wiola_workspace_watcher
)
wiola_add_test(
    app_workspace_reconciliation_test
    SOURCES workspace_reconciliation_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_workspace_scan_publisher_test
    SOURCES workspace_scan_publisher_test.cpp
    LIBS wiola_workspace_scans
)
wiola_add_test(
    app_workspace_scanner_test
    SOURCES workspace_scanner_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_m3u_playlist_test
    SOURCES m3u_playlist_test.cpp
    LIBS wiola_m3u
)
wiola_add_test(
    app_playlist_transfer_test
    SOURCES playlist_transfer_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_active_playlist_test
    SOURCES active_playlist_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_playback_queue_test
    SOURCES playback_queue_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_playback_service_test
    SOURCES playback_service_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_standard_metadata_commands_test
    SOURCES standard_metadata_commands_test.cpp
    LIBS wiola_app
)
wiola_add_test(
    app_search_index_test
    SOURCES search_index_test.cpp
    LIBS wiola_app
)

if(WIOLA_BUILD_GUI)
    wiola_add_test(
        app_player_application_test
        SOURCES player_application_test.cpp
        LIBS wiola_player_application
    )
    wiola_add_test(
        app_saved_playlist_commands_test
        SOURCES saved_playlist_commands_test.cpp
        LIBS wiola_saved_playlists wiola_gui
    )
    wiola_add_test(
        app_queue_library_migration_test
        SOURCES queue_library_migration_test.cpp
        LIBS wiola_queue_migration
    )
endif()
