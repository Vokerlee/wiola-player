add_subdirectory(audio_decoder)

wiola_add_library(wiola_media SOURCES local_file_stream.cpp)
target_link_libraries(wiola_media PUBLIC wiola::audio_decoder)
