wiola_add_test(
    library_identity_test
    SOURCES identity_test.cpp
    LIBS wiola_library
)
wiola_add_test(
    library_location_test
    SOURCES location_test.cpp
    LIBS wiola_library
)
wiola_add_test(library_records_test SOURCES records_test.cpp LIBS wiola_library)
wiola_add_test(
    library_in_memory_repository_test
    SOURCES in_memory_repository_test.cpp
    LIBS wiola_library
)
wiola_add_test(
    library_fingerprint_test
    SOURCES fingerprint_test.cpp
    LIBS wiola_library
)
