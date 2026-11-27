wiola_add_test(
    presentation_track_sort_test
    SOURCES track_sort_test.cpp
    LIBS
        wiola::presentation
        wiola_playback_queue
        wiola_library
        wiola::local_media
)

wiola_add_test(
    presentation_track_sorter_test
    SOURCES track_sorter_test.cpp
    LIBS
        wiola::presentation
        wiola_playback_queue
        wiola_library
        wiola::local_media
)

wiola_add_test(
    presentation_session_documents_test
    SOURCES session_documents_test.cpp
    LIBS wiola::presentation
)

wiola_add_test(
    presentation_track_projection_test
    SOURCES track_projection_test.cpp
    LIBS wiola::presentation
)

wiola_add_test(
    presentation_track_view_test
    SOURCES track_view_test.cpp
    LIBS wiola::presentation
)

wiola_add_test(
    presentation_saved_playlist_rows_test
    SOURCES saved_playlist_rows_test.cpp
    LIBS wiola::presentation
)

wiola_add_test(
    presentation_workspace_rows_test
    SOURCES workspace_rows_test.cpp
    LIBS wiola::presentation
)

wiola_add_test(
    presentation_queue_rows_test
    SOURCES queue_rows_test.cpp
    LIBS wiola::presentation wiola_playback_queue wiola::local_media
)
