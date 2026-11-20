wiola_add_library(
    wiola_gui
    SOURCES
        clock.cpp
        artwork_cache.cpp
        custom_tag_editor.cpp
        equalizer_control.cpp
        equalizer_panel.cpp
        library_json.cpp
        library_navigation.cpp
        library_storage.cpp
        main_window.cpp
        now_playing_document.cpp
        playlist_control.cpp
        track_inspector.cpp
        track_table_model.cpp
        track_table_view.cpp
        track_sort.cpp
        player_actions.cpp
        player_presenter.cpp
        player_settings.cpp
        saved_playlist_document.cpp
        saved_playlist_manager.cpp
        saved_playlist_storage.cpp
        seek_bar.cpp
        volume_control.cpp
        workspace_document.cpp
    LIBS
        wiola_app
        wiola_codec
        wiola_engine
        wiola_library
        wiola_media_revisions
        wiola_m3u
        wiola_saved_playlists
        wiola_workspace_scans
        wiola_workspace_watcher
)

# Qt needs its own generated sources, and its headers are part of using this library.
set_target_properties(wiola_gui PROPERTIES AUTOMOC ON)
target_link_libraries(wiola_gui PUBLIC Qt6::Widgets)
