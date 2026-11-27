# Everything the project fetches, and the versions it is fixed to. Declared in one place so that
# the set of dependencies can be read without opening every subdirectory.

include(FetchContent)

# Dependency option() calls must honor the scoped normal variables set below.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(NOT WIOLA_COMPONENT_BUILD)
    include(ffmpeg)

    if(WIOLA_BUILD_GUI)
        find_package(Qt6 QUIET COMPONENTS Widgets)

        if(NOT Qt6_FOUND)
            message(
                FATAL_ERROR
                "Qt6 Widgets was not found. Install qt6-base-dev for a native Linux build, "
                "set QT_MINGW_ROOT for the Windows preset, or select a portable component "
                "with -DWIOLA_COMPONENTS=media to work without Qt. "
                "For another Qt installation, set CMAKE_PREFIX_PATH to its prefix."
            )
        endif()
    endif()

    set(MINIAUDIO_NO_LIBVORBIS ON)

    set(MINIAUDIO_NO_LIBOPUS ON)

    FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.25
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(miniaudio)
endif()

# Nothing but the tests needs this, and a build without them should not go to the network for it.
if(WIOLA_BUILD_TESTS)
    if(WIOLA_BUILD_GUI)
        find_package(Qt6 REQUIRED COMPONENTS Test)
    endif()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES GTest
    )

    FetchContent_MakeAvailable(googletest)
endif()
