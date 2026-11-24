wiola_add_test(
    engine_pipeline_ownership_test
    SOURCES pipeline_ownership_test.cpp
    LIBS wiola_engine_pipeline wiola_test_allocation_failure wiola::audio_dsp
)
