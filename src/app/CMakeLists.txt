include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/headers.cmake")

find_package(Threads REQUIRED)

add_library(wiola_snapshot_writer INTERFACE)

add_library(wiola::snapshot_writer ALIAS wiola_snapshot_writer)

target_compile_features(wiola_snapshot_writer INTERFACE cxx_std_23)

target_link_libraries(wiola_snapshot_writer INTERFACE wiola::library)

wiola_target_headers(
    wiola_snapshot_writer
    wiola/app/library
    HEADERS library/snapshot_writer.hpp
)

add_library(wiola_library_persistence STATIC library/library_persistence.cpp)

add_library(wiola::library_persistence ALIAS wiola_library_persistence)

set_target_properties(wiola_library_persistence PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(
    wiola_library_persistence
    PUBLIC wiola::snapshot_writer Threads::Threads
)

wiola_target_headers(
    wiola_library_persistence
    wiola/app/library
    HEADERS library/library_persistence.hpp
)

if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_library_persistence)
endif()

if(NOT WIOLA_COMPONENT_BUILD)
    wiola_add_library(wiola_local_identity SOURCES library/local_identity.cpp)

    add_library(wiola::local_identity ALIAS wiola_local_identity)

    target_link_libraries(wiola_local_identity PUBLIC wiola::library)

    wiola_add_library(
        wiola_app
        SOURCES
            library/search_index.cpp
            library/artwork_store.cpp
            library/location_index.cpp
            library/tag_key_catalog.cpp
            metadata/background_metadata.cpp
            metadata/background_metadata_writes.cpp
            metadata/metadata_write_job.cpp
            metadata/custom_tag_commands.cpp
            metadata/media_replacement.cpp
            metadata/metadata_conflicts.cpp
            metadata/metadata_merge.cpp
            metadata/metadata_edit_session.cpp
            metadata/metadata_edit_service.cpp
            playback/active_playlist.cpp
            playback/playback_queue.cpp
            playback/playback_service.cpp
            playback/playlist_transfer.cpp
            workspace/workspace_discovery.cpp
            workspace/workspace_reconciliation.cpp
            workspace/workspace_scanner.cpp
        LIBS
            wiola::local_identity
            wiola::file_replacement
            wiola::file_fingerprint
            wiola::local_media
    )

    target_link_libraries(
        wiola_app
        PUBLIC wiola::library wiola::playback wiola::audio_dsp
    )
endif()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
