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
    DATA "${CMAKE_SOURCE_DIR}/tests/codec/data"
)
wiola_add_test(
    app_saved_playlist_commands_test
    SOURCES library/saved_playlist_commands_test.cpp
    LIBS wiola_saved_playlists wiola_persistence_qt
)
wiola_add_test(
    app_queue_library_migration_test
    SOURCES library/queue_library_migration_test.cpp
    LIBS wiola_queue_migration
)

wiola_add_test(
    desktop_queued_delivery_test
    SOURCES queued_delivery_test.cpp
    LIBS wiola_desktop_delivery
)

wiola_add_test(
    gui_playlist_control_test
    SOURCES playback/queue_settings_test.cpp
    LIBS wiola_playback_preferences
)
wiola_add_test(
    gui_equalizer_control_test
    SOURCES playback/equalizer_control_test.cpp
    LIBS wiola_playback_preferences
)
wiola_add_test(
    gui_volume_control_test
    SOURCES playback/volume_control_test.cpp
    LIBS wiola_playback_preferences
)

wiola_add_test(
    desktop_metadata_write_service_test
    SOURCES metadata/metadata_write_service_test.cpp
    LIBS wiola_metadata_writes wiola_codec wiola_qt_platform
    DATA "${CMAKE_SOURCE_DIR}/tests/codec/data"
)
