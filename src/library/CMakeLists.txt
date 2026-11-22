wiola_add_library(
    wiola_library
    SOURCES
        fingerprint.cpp
        in_memory_repository.cpp
        location.cpp
        library_changes.cpp
        metadata.cpp
        metadata_patch.cpp
        repository_transaction.cpp
        search.cpp
        tags.cpp
)

target_link_libraries(wiola_library PRIVATE Threads::Threads)
