wiola_add_library(
    wiola_engine_load
    SOURCES loader.cpp metadata_coordinator.cpp tag_reader.cpp
    LIBS wiola_codec
)
