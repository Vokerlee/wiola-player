include(headers)

add_library(wiola_file_fingerprint STATIC fingerprint.cpp)

add_library(wiola::file_fingerprint ALIAS wiola_file_fingerprint)

target_compile_features(wiola_file_fingerprint PUBLIC cxx_std_23)

wiola_target_headers(
    wiola_file_fingerprint
    wiola/media/local
    HEADERS fingerprint.hpp
)

wiola_apply_build_controls(wiola_file_fingerprint)

wiola_add_library(wiola_file_replacement SOURCES file_replacement.cpp)

add_library(wiola::file_replacement ALIAS wiola_file_replacement)

wiola_target_headers(
    wiola_file_replacement
    wiola/media/local
    HEADERS file_replacement.hpp
)

target_link_libraries(wiola_file_replacement PUBLIC wiola::media)

wiola_add_library(wiola_local_media SOURCES file_stream.cpp resource.cpp)

add_library(wiola::local_media ALIAS wiola_local_media)

target_link_libraries(wiola_local_media PUBLIC wiola::media)

wiola_target_headers(
    wiola_local_media
    wiola/media/local
    HEADERS file_stream.hpp resource.hpp
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
