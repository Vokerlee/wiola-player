wiola_add_library(
    wiola_gui_model
    SOURCES
        tracks/track_row.cpp
        tracks/track_sort.cpp
        tracks/track_sorter.cpp
        tracks/track_view_state.cpp
        tracks/track_view_pipeline.cpp
        documents/queue_rows.cpp
        documents/saved_playlist_rows.cpp
        documents/workspace_rows.cpp
    LIBS wiola_app wiola_library wiola_engine
)
add_executable(wiola_model_boundary EXCLUDE_FROM_ALL boundary.cpp)
target_link_libraries(
    wiola_model_boundary
    PRIVATE wiola_gui_model wiola_sanitizer wiola_coverage
)
