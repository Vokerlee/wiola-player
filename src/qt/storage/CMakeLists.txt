wiola_add_library(
    wiola_qt_files
    SOURCES atomic_file_store.cpp
    LIBS wiola::qt_paths
)
add_library(wiola::qt_files ALIAS wiola_qt_files)
target_link_libraries(wiola_qt_files PUBLIC Qt6::Core)

wiola_add_library(
    wiola_qt_library_store
    SOURCES library_json.cpp library_store.cpp snapshot_writer.cpp
    LIBS wiola::qt_paths
)
add_library(wiola::qt_library_store ALIAS wiola_qt_library_store)
target_link_libraries(
    wiola_qt_library_store
    PUBLIC Qt6::Core wiola::qt_files wiola::library wiola::snapshot_writer
)
