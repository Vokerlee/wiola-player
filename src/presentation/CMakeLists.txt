include(headers)

find_package(Threads REQUIRED)

wiola_add_library(
    wiola_presentation
    SOURCES
        tracks/track_row.cpp
        tracks/track_sort.cpp
        tracks/track_sorter.cpp
        tracks/track_view_state.cpp
        tracks/track_view_pipeline.cpp
        tracks/track_projection.cpp
        documents/queue_rows.cpp
        documents/session_documents.cpp
        documents/saved_playlist_rows.cpp
        documents/workspace_rows.cpp
    LIBS wiola::local_media
)

add_library(wiola::presentation ALIAS wiola_presentation)

wiola_target_headers(
    wiola_presentation
    wiola/presentation/documents
    HEADERS
        documents/document_id.hpp
        documents/queue_rows.hpp
        documents/saved_playlist_rows.hpp
        documents/session_documents.hpp
        documents/workspace_rows.hpp
)

wiola_target_headers(
    wiola_presentation
    wiola/presentation/tracks
    HEADERS
        tracks/browser_view_state.hpp
        tracks/track_column_identity.hpp
        tracks/track_commands.hpp
        tracks/track_identity.hpp
        tracks/track_projection.hpp
        tracks/track_row.hpp
        tracks/track_sort.hpp
        tracks/track_sorter.hpp
        tracks/track_view_pipeline.hpp
        tracks/track_view_state.hpp
)

add_executable(wiola_model_boundary EXCLUDE_FROM_ALL boundary.cpp)

target_link_libraries(
    wiola_model_boundary
    PRIVATE wiola::presentation wiola_sanitizer wiola_coverage
)

target_link_libraries(
    wiola_presentation
    PUBLIC wiola::library wiola_app wiola::playback_api Threads::Threads
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
