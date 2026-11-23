wiola_add_library(
    wiola_persistence_qt
    SOURCES qt/library_json.cpp qt/library_store.cpp qt/atomic_file_store.cpp
    LIBS wiola_library wiola_qt_platform
)
target_link_libraries(wiola_persistence_qt PUBLIC Qt6::Core)
