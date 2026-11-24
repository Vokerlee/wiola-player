wiola_add_library(
    wiola_desktop_delivery
    SOURCES queued_delivery.cpp
    LIBS Threads::Threads
)
target_link_libraries(wiola_desktop_delivery PUBLIC Qt6::Core)
wiola_add_library(
    wiola_workspace_scans
    SOURCES workspace/workspace_scan_publisher.cpp
    LIBS wiola_app wiola_desktop_delivery
)
target_link_libraries(wiola_workspace_scans PUBLIC Qt6::Core wiola_app)
wiola_add_library(
    wiola_media_revisions
    SOURCES metadata/media_revision_publisher.cpp
    LIBS wiola_app wiola_desktop_delivery
)
target_link_libraries(wiola_media_revisions PUBLIC Qt6::Core)
wiola_add_library(
    wiola_workspace_watcher
    SOURCES workspace/workspace_watcher.cpp
    LIBS wiola_app wiola_library wiola_qt_platform
)
target_link_libraries(wiola_workspace_watcher PUBLIC Qt6::Core)
wiola_add_library(
    wiola_m3u
    SOURCES library/m3u_playlist.cpp
    LIBS wiola_library wiola_qt_platform
)
target_link_libraries(wiola_m3u PUBLIC Qt6::Core)
wiola_add_library(
    wiola_saved_playlists
    SOURCES library/saved_playlist_commands.cpp
    LIBS wiola_library
)
target_link_libraries(wiola_saved_playlists PUBLIC Qt6::Core)
wiola_add_library(
    wiola_player_application
    SOURCES player_application.cpp
    LIBS
        wiola_library_session
        wiola_playback_preferences
        wiola_codec
        wiola_audio_device
)
set_target_properties(wiola_player_application PROPERTIES AUTOMOC ON)
target_link_libraries(wiola_player_application PUBLIC Qt6::Core wiola_app)
wiola_add_library(
    wiola_queue_migration
    SOURCES library/queue_library_migration.cpp
    LIBS wiola_persistence_qt wiola_library wiola_qt_platform wiola_qt_settings
)
target_link_libraries(wiola_queue_migration PUBLIC Qt6::Core)

wiola_add_library(
    wiola_metadata_writes
    SOURCES metadata/metadata_write_service.cpp
    LIBS wiola_app wiola_desktop_delivery
)
target_link_libraries(wiola_metadata_writes PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_library_session
    SOURCES library/library_session.cpp
    LIBS
        wiola_library_persistence
        wiola_app
        wiola_saved_playlists
        wiola_persistence_qt
        wiola_media_revisions
        wiola_metadata_writes
        wiola_workspace_scans
        wiola_workspace_watcher
        wiola_codec
        wiola_qt_platform
)
set_target_properties(wiola_library_session PROPERTIES AUTOMOC ON)
target_link_libraries(wiola_library_session PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_playback_preferences
    SOURCES
        playback/playback_preferences.cpp
        playback/queue_settings.cpp
        playback/volume_control.cpp
        playback/equalizer_control.cpp
    LIBS wiola_app wiola_qt_settings wiola_session_preferences
)
target_link_libraries(wiola_playback_preferences PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_session_preferences
    SOURCES session/session_state.cpp session/session_preferences.cpp
    LIBS wiola_persistence_qt wiola_library_session wiola_gui_model
)
set_target_properties(wiola_session_preferences PROPERTIES AUTOMOC ON)
target_link_libraries(wiola_session_preferences PUBLIC Qt6::Core wiola_app)

wiola_add_library(
    wiola_library_documents
    SOURCES library/library_documents.cpp
    LIBS wiola_library wiola_m3u wiola_persistence_qt wiola_qt_platform
)
target_link_libraries(wiola_library_documents PUBLIC Qt6::Core)

wiola_add_library(
    wiola_library_persistence
    SOURCES library/library_persistence.cpp
    LIBS wiola_library wiola_persistence_qt wiola_desktop_delivery
)
target_link_libraries(wiola_library_persistence PUBLIC Qt6::Core)
