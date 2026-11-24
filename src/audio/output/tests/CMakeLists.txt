find_package(Threads REQUIRED)

wiola_add_test(
    audio_output_test
    SOURCES output_test.cpp
    LIBS wiola_audio_output Threads::Threads
)
