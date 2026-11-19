wiola_add_library(
    wiola_app
    SOURCES
        active_playlist.cpp
        background_metadata.cpp
        custom_tag_commands.cpp
        metadata_merge.cpp
        playback_queue.cpp
        playback_service.cpp
        playlist_transfer.cpp
        search_index.cpp
        workspace_reconciliation.cpp
        workspace_scanner.cpp
    LIBS wiola_engine wiola_library
)

if(WIOLA_BUILD_GUI)
    wiola_add_library(
        wiola_workspace_scans
        SOURCES workspace_scan_publisher.cpp
        LIBS wiola_app
    )
    target_link_libraries(wiola_workspace_scans PUBLIC Qt6::Core)

    wiola_add_library(
        wiola_media_revisions
        SOURCES media_revision_publisher.cpp
        LIBS wiola_app
    )
    target_link_libraries(wiola_media_revisions PUBLIC Qt6::Core)

    wiola_add_library(
        wiola_workspace_watcher
        SOURCES workspace_watcher.cpp
        LIBS wiola_library
    )
    target_link_libraries(wiola_workspace_watcher PUBLIC Qt6::Core)

    wiola_add_library(wiola_m3u SOURCES m3u_playlist.cpp LIBS wiola_library)
    target_link_libraries(wiola_m3u PUBLIC Qt6::Core)

    wiola_add_library(
        wiola_player_application
        SOURCES player_application.cpp
        LIBS wiola_gui
    )
    set_target_properties(wiola_player_application PROPERTIES AUTOMOC ON)
    target_link_libraries(wiola_player_application PUBLIC Qt6::Widgets)
    wiola_add_library(
        wiola_saved_playlists
        SOURCES saved_playlist_commands.cpp
        LIBS wiola_library
    )
    target_link_libraries(wiola_saved_playlists PUBLIC Qt6::Core)

    wiola_add_library(
        wiola_queue_migration
        SOURCES queue_library_migration.cpp
        LIBS wiola_gui wiola_library
    )
    target_link_libraries(wiola_queue_migration PUBLIC Qt6::Core)
endif()
