if(NOT WIOLA_COMPONENT_BUILD OR audio_decoder IN_LIST WIOLA_SELECTED_COMPONENTS)
    add_subdirectory(audio_decoder)
endif()
if(WIOLA_COMPONENT_BUILD)
    return()
endif()

wiola_add_library(wiola_media SOURCES local_file_stream.cpp)
target_link_libraries(wiola_media PUBLIC wiola::audio_decoder)
