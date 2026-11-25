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
