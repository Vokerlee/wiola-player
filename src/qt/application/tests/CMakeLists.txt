wiola_add_test(
    app_media_revision_publisher_test
    SOURCES metadata/media_revision_publisher_test.cpp
    LIBS wiola_media_revisions
)

target_link_libraries(
    app_media_revision_publisher_test
    PRIVATE wiola::local_media
)

wiola_add_test(
    app_workspace_watcher_test
    SOURCES workspace/workspace_watcher_test.cpp
    LIBS wiola_workspace_watcher
)

target_link_libraries(app_workspace_watcher_test PRIVATE wiola::qt_paths)

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

target_link_libraries(app_m3u_playlist_test PRIVATE wiola::qt_paths)

wiola_add_test(
    app_player_application_test
    SOURCES player_application_test.cpp
    LIBS wiola_player_application wiola::qt_widgets wiola::ffmpeg
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)

target_link_libraries(
    app_player_application_test
    PRIVATE wiola::file_fingerprint wiola::local_media wiola::qt_paths
)

wiola_add_test(
    app_saved_playlist_commands_test
    SOURCES library/saved_playlist_commands_test.cpp
    LIBS wiola_saved_playlists wiola::qt_library_store
)

wiola_add_test(
    app_queue_library_migration_test
    SOURCES library/queue_library_migration_test.cpp
    LIBS wiola_queue_migration wiola_qt_settings
)

target_link_libraries(app_queue_library_migration_test PRIVATE wiola::qt_paths)

wiola_add_test(
    gui_playlist_control_test
    SOURCES playback/queue_settings_test.cpp
    LIBS wiola_playback_preferences wiola::ffmpeg
)

target_link_libraries(gui_playlist_control_test PRIVATE wiola::local_media)

wiola_add_test(
    desktop_metadata_write_service_test
    SOURCES metadata/metadata_write_service_test.cpp
    LIBS wiola_metadata_writes wiola::ffmpeg wiola::qt_paths
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)

target_link_libraries(
    desktop_metadata_write_service_test
    PRIVATE wiola::file_fingerprint
)

wiola_add_test(
    desktop_session_preferences_test
    SOURCES session/session_preferences_test.cpp
    LIBS wiola_session_preferences wiola_library_session wiola::ffmpeg
)

target_link_libraries(desktop_session_preferences_test PRIVATE wiola::qt_paths)

wiola_add_test(
    desktop_library_documents_test
    SOURCES library/library_documents_test.cpp
    LIBS wiola_library_documents
)

target_link_libraries(desktop_library_documents_test PRIVATE wiola::qt_paths)

wiola_add_test(
    desktop_library_persistence_test
    SOURCES library/library_persistence_test.cpp
    LIBS
        wiola::library_persistence
        wiola::qt_library_store
        wiola::qt_delivery
        wiola::qt_paths
)

wiola_add_test(
    desktop_library_lifetime_test
    SOURCES library/library_lifetime_test.cpp
    LIBS wiola_library_session wiola::ffmpeg Qt6::Test
    DATA "${PROJECT_SOURCE_DIR}/src/media/codecs/tests/data"
)

target_link_libraries(
    desktop_library_lifetime_test
    PRIVATE wiola::file_fingerprint wiola::local_media wiola::qt_paths
)
