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
target_link_libraries(wiola_workspace_scans PUBLIC Qt6::Core)
wiola_add_library(
    wiola_media_revisions
    SOURCES metadata/media_revision_publisher.cpp
    LIBS wiola_app wiola_desktop_delivery
)
target_link_libraries(wiola_media_revisions PUBLIC Qt6::Core)
wiola_add_library(
    wiola_workspace_watcher
    SOURCES workspace/workspace_watcher.cpp
    LIBS wiola_library wiola_qt_platform
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
    LIBS wiola_gui
)
set_target_properties(wiola_player_application PROPERTIES AUTOMOC ON)
target_link_libraries(wiola_player_application PUBLIC Qt6::Widgets)
wiola_add_library(
    wiola_queue_migration
    SOURCES library/queue_library_migration.cpp
    LIBS wiola_persistence_qt wiola_library wiola_qt_platform wiola_qt_settings
)
target_link_libraries(wiola_queue_migration PUBLIC Qt6::Core)
