wiola_add_test(
    app_media_revision_publisher_test
    SOURCES metadata/media_revision_publisher_test.cpp
    LIBS wiola_media_revisions
)
wiola_add_test(
    app_workspace_watcher_test
    SOURCES workspace/workspace_watcher_test.cpp
    LIBS wiola_workspace_watcher
)
wiola_add_test(
    app_workspace_scan_publisher_test
    SOURCES workspace/workspace_scan_publisher_test.cpp
    LIBS wiola_workspace_scans
)
wiola_add_test(
    app_m3u_playlist_test
    SOURCES library/m3u_playlist_test.cpp
    LIBS wiola_m3u
)
wiola_add_test(
    app_player_application_test
    SOURCES player_application_test.cpp
    LIBS wiola_player_application wiola_gui
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)
wiola_add_test(
    app_saved_playlist_commands_test
    SOURCES library/saved_playlist_commands_test.cpp
    LIBS wiola_saved_playlists wiola_persistence_qt
)
wiola_add_test(
    app_queue_library_migration_test
    SOURCES library/queue_library_migration_test.cpp
    LIBS wiola_queue_migration wiola_qt_settings
)

wiola_add_test(
    desktop_queued_delivery_test
    SOURCES queued_delivery_test.cpp
    LIBS wiola_desktop_delivery
)

wiola_add_test(
    gui_playlist_control_test
    SOURCES playback/queue_settings_test.cpp
    LIBS wiola_playback_preferences wiola_codec
)
wiola_add_test(
    gui_equalizer_control_test
    SOURCES playback/equalizer_control_test.cpp
    LIBS wiola_playback_preferences wiola_codec
)
wiola_add_test(
    gui_volume_control_test
    SOURCES playback/volume_control_test.cpp
    LIBS wiola_playback_preferences wiola_codec
)

wiola_add_test(
    desktop_metadata_write_service_test
    SOURCES metadata/metadata_write_service_test.cpp
    LIBS wiola_metadata_writes wiola_codec wiola::qt_paths
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)

wiola_add_test(
    desktop_session_preferences_test
    SOURCES session/session_preferences_test.cpp
    LIBS wiola_session_preferences wiola_codec
)

wiola_add_test(
    desktop_library_documents_test
    SOURCES library/library_documents_test.cpp
    LIBS wiola_library_documents
)

wiola_add_test(
    desktop_library_persistence_test
    SOURCES library/library_persistence_test.cpp
    LIBS wiola_library_persistence
)

wiola_add_test(
    desktop_library_lifetime_test
    SOURCES library/library_lifetime_test.cpp
    LIBS wiola_library_session wiola_codec Qt6::Test
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)
