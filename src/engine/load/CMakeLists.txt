find_package(Threads REQUIRED)

wiola_add_library(
    wiola_engine_load
    SOURCES loader.cpp metadata_coordinator.cpp tag_reader.cpp
    LIBS Threads::Threads
)
target_link_libraries(wiola_engine_load PUBLIC wiola::media)
