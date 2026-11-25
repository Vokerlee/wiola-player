wiola_add_test(
    media_local_file_stream_test
    SOURCES local_file_stream_test.cpp
    LIBS wiola::local_media
)

wiola_add_test(
    media_local_resource_test
    SOURCES resource_test.cpp
    LIBS wiola::local_media
)

wiola_add_test(
    media_file_replacement_test
    SOURCES file_replacement_test.cpp
    LIBS wiola::file_replacement wiola_library
)
