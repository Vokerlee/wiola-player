wiola_add_library(
    wiola_app
    SOURCES active_playlist.cpp playback_queue.cpp playback_service.cpp
    LIBS wiola_engine wiola_library
)

if(WIOLA_BUILD_GUI)
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
