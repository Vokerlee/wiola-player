wiola_add_library(
    wiola_local_identity
    SOURCES library/local_identity.cpp
    LIBS wiola_library
)
add_library(wiola::local_identity ALIAS wiola_local_identity)

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
        wiola_library
        wiola::local_identity
        wiola::file_replacement
        wiola::file_fingerprint
        wiola::local_media
)
target_link_libraries(wiola_app PUBLIC wiola::playback wiola::audio_dsp)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
