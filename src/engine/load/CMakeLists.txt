find_package(Threads REQUIRED)

wiola_add_library(
    wiola_engine_metadata
    SOURCES metadata_coordinator.cpp tag_reader.cpp
    LIBS Threads::Threads
)
target_link_libraries(
    wiola_engine_metadata
    PUBLIC wiola::media wiola::playback_api wiola_playback_queue
)
