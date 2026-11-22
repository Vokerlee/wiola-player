wiola_add_test(
    library_metadata_test
    SOURCES metadata_test.cpp
    LIBS wiola_library
)
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
wiola_add_test(library_tags_test SOURCES tags_test.cpp LIBS wiola_library)
wiola_add_test(library_search_test SOURCES search_test.cpp LIBS wiola_library)
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

wiola_add_test(
    library_repository_transaction_test
    SOURCES repository_transaction_test.cpp
    LIBS wiola_library
)

wiola_add_test(
    library_metadata_patch_test
    SOURCES metadata_patch_test.cpp
    LIBS wiola_library
)
