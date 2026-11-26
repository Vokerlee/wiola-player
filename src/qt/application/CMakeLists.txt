wiola_add_library(
    wiola_workspace_scans
    SOURCES workspace/workspace_scan_publisher.cpp
    LIBS wiola::qt_delivery
)

target_link_libraries(wiola_workspace_scans PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_media_revisions
    SOURCES metadata/media_revision_publisher.cpp
    LIBS wiola::qt_delivery
)

target_link_libraries(wiola_media_revisions PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_workspace_watcher
    SOURCES workspace/workspace_watcher.cpp
    LIBS wiola_library wiola::qt_paths
)

target_link_libraries(wiola_workspace_watcher PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_m3u
    SOURCES library/m3u_playlist.cpp
    LIBS wiola::local_identity wiola::qt_paths
)

target_link_libraries(wiola_m3u PUBLIC Qt6::Core wiola::library)

wiola_add_library(
    wiola_player_application
    SOURCES player_application.cpp
    LIBS wiola::local_media
)

set_target_properties(wiola_player_application PROPERTIES AUTOMOC ON)

target_link_libraries(
    wiola_player_application
    PUBLIC Qt6::Core wiola_app wiola_library_session wiola_playback_preferences
)

wiola_add_library(
    wiola_queue_migration
    SOURCES library/queue_library_migration.cpp
    LIBS
        wiola::qt_library_store
        wiola::local_identity
        wiola::qt_paths
        wiola_qt_settings
)

target_link_libraries(wiola_queue_migration PUBLIC Qt6::Core wiola::library)

wiola_add_library(
    wiola_metadata_writes
    SOURCES metadata/metadata_write_service.cpp
    LIBS wiola::qt_delivery
)

target_link_libraries(wiola_metadata_writes PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_library_session
    SOURCES library/library_session.cpp
    LIBS
        wiola_library_persistence
        wiola::file_fingerprint
        wiola_media_revisions
        wiola_metadata_writes
        wiola_workspace_scans
        wiola_workspace_watcher
        wiola_codec
        wiola::qt_paths
)

set_target_properties(wiola_library_session PROPERTIES AUTOMOC ON)

target_link_libraries(
    wiola_library_session
    PUBLIC
        Qt6::Core
        wiola_app
        wiola::qt_library_store
        wiola::qt_delivery
        wiola::playlist_commands
)

wiola_add_library(
    wiola_playback_preferences
    SOURCES playback/playback_preferences.cpp
    LIBS wiola_session_preferences wiola::local_media
)

target_link_libraries(
    wiola_playback_preferences
    PUBLIC Qt6::Core wiola_app wiola::qt_settings
)

wiola_add_library(
    wiola_session_preferences
    SOURCES session/session_state.cpp session/session_preferences.cpp
    LIBS wiola::qt_files wiola_library_session wiola::presentation
)

set_target_properties(wiola_session_preferences PROPERTIES AUTOMOC ON)

target_link_libraries(wiola_session_preferences PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_library_documents
    SOURCES library/library_documents.cpp
    LIBS wiola_m3u wiola::qt_files wiola::qt_paths
)

target_link_libraries(wiola_library_documents PUBLIC Qt6::Core wiola::library)
