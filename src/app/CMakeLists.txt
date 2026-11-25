include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/headers.cmake")

if(
    NOT WIOLA_COMPONENT_BUILD
    OR library_persistence IN_LIST WIOLA_SELECTED_COMPONENTS
)
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

    add_library(
        wiola_library_persistence
        STATIC
        library/library_persistence.cpp
    )

    add_library(wiola::library_persistence ALIAS wiola_library_persistence)

    set_target_properties(
        wiola_library_persistence
        PROPERTIES CXX_EXTENSIONS OFF
    )

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
endif()

if(
    NOT WIOLA_COMPONENT_BUILD
    OR playlist_commands IN_LIST WIOLA_SELECTED_COMPONENTS
)
    add_library(
        wiola_saved_playlists
        STATIC
        library/saved_playlist_commands.cpp
        library/playlist_name.cpp
    )

    add_library(wiola::playlist_commands ALIAS wiola_saved_playlists)

    set_target_properties(wiola_saved_playlists PROPERTIES CXX_EXTENSIONS OFF)

    target_link_libraries(wiola_saved_playlists PUBLIC wiola::library)

    wiola_target_headers(
        wiola_saved_playlists
        wiola/app/library
        HEADERS library/saved_playlist_commands.hpp library/playlist_name.hpp
    )

    wiola_apply_build_controls(wiola_saved_playlists)
endif()

if(
    NOT WIOLA_COMPONENT_BUILD
    OR metadata_edits IN_LIST WIOLA_SELECTED_COMPONENTS
)
    add_library(
        wiola_metadata_edits
        STATIC
        metadata/metadata_edit_service.cpp
        metadata/metadata_edit_session.cpp
        metadata/metadata_conflicts.cpp
        metadata/metadata_merge.cpp
    )

    add_library(wiola::metadata_edits ALIAS wiola_metadata_edits)

    set_target_properties(wiola_metadata_edits PROPERTIES CXX_EXTENSIONS OFF)

    target_link_libraries(wiola_metadata_edits PUBLIC wiola::library)

    wiola_target_headers(
        wiola_metadata_edits
        wiola/app/metadata
        HEADERS
            metadata/metadata_edit_service.hpp
            metadata/metadata_edit_session.hpp
            metadata/metadata_conflicts.hpp
            metadata/metadata_merge.hpp
    )

    wiola_apply_build_controls(wiola_metadata_edits)
endif()

if(NOT WIOLA_COMPONENT_BUILD OR workspace IN_LIST WIOLA_SELECTED_COMPONENTS)
    add_library(wiola_local_identity STATIC library/local_identity.cpp)

    add_library(wiola::local_identity ALIAS wiola_local_identity)

    set_target_properties(wiola_local_identity PROPERTIES CXX_EXTENSIONS OFF)

    target_link_libraries(wiola_local_identity PUBLIC wiola::library)

    wiola_target_headers(
        wiola_local_identity
        wiola/app/library
        HEADERS library/local_identity.hpp
    )

    wiola_apply_build_controls(wiola_local_identity)

    add_library(
        wiola_workspace
        STATIC
        workspace/workspace_discovery.cpp
        workspace/workspace_reconciliation.cpp
    )

    add_library(wiola::workspace ALIAS wiola_workspace)

    set_target_properties(wiola_workspace PROPERTIES CXX_EXTENSIONS OFF)

    target_link_libraries(
        wiola_workspace
        PUBLIC wiola::library wiola::local_identity
    )

    wiola_target_headers(
        wiola_workspace
        wiola/app/workspace
        HEADERS
            workspace/workspace_files.hpp
            workspace/workspace_discovery.hpp
            workspace/workspace_reconciliation.hpp
    )

    wiola_apply_build_controls(wiola_workspace)
endif()

if(NOT WIOLA_COMPONENT_BUILD)
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
            playback/active_playlist.cpp
            playback/playback_queue.cpp
            playback/playback_service.cpp
            playback/volume_control.cpp
            playback/equalizer_control.cpp
            playback/playlist_transfer.cpp
            workspace/native_workspace_files.cpp
            workspace/workspace_scanner.cpp
        LIBS
            wiola::local_identity
            wiola::file_replacement
            wiola::file_fingerprint
            wiola::local_media
    )

    target_link_libraries(
        wiola_app
        PUBLIC
            wiola::library
            wiola::playback
            wiola::audio_dsp
            wiola::metadata_edits
            wiola::workspace
    )
endif()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
