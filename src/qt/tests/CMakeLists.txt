wiola_add_test(
    qt_path_adapter_test
    SOURCES path_adapter_test.cpp
    LIBS wiola::qt_paths
)

wiola_add_test(
    qt_queued_delivery_test
    SOURCES queued_delivery_test.cpp
    LIBS wiola::qt_delivery
)
add_subdirectory(compatibility)
