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
