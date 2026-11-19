wiola_add_library(
    wiola_gui
    SOURCES
        clock.cpp
        artwork_cache.cpp
        equalizer_control.cpp
        equalizer_panel.cpp
        library_json.cpp
        library_storage.cpp
        main_window.cpp
        now_playing_document.cpp
        playlist_control.cpp
        track_table_model.cpp
        track_table_view.cpp
        player_actions.cpp
        player_presenter.cpp
        player_settings.cpp
        seek_bar.cpp
        volume_control.cpp
    LIBS wiola_app wiola_codec wiola_engine
)

# Qt needs its own generated sources, and its headers are part of using this library.
set_target_properties(wiola_gui PROPERTIES AUTOMOC ON)
target_link_libraries(wiola_gui PUBLIC Qt6::Widgets)
