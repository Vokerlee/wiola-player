wiola_add_library(
    wiola_presentation
    SOURCES
        tracks/track_row.cpp
        tracks/track_sort.cpp
        tracks/track_sorter.cpp
        tracks/track_view_state.cpp
        tracks/track_view_pipeline.cpp
        documents/queue_rows.cpp
        documents/saved_playlist_rows.cpp
        documents/workspace_rows.cpp
    LIBS wiola::local_media
)
add_library(wiola::presentation ALIAS wiola_presentation)
add_executable(wiola_model_boundary EXCLUDE_FROM_ALL boundary.cpp)
target_link_libraries(
    wiola_model_boundary
    PRIVATE wiola::presentation wiola_sanitizer wiola_coverage
)

target_link_libraries(
    wiola_presentation
    PUBLIC wiola::library wiola_app wiola::playback_api
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
