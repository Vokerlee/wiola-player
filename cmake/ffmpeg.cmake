include(ExternalProject)

include(ProcessorCount)

set(wiola_ffmpeg_prefix "${CMAKE_BINARY_DIR}/_deps/ffmpeg-install")

set(wiola_ffmpeg_source "${CMAKE_BINARY_DIR}/_deps/ffmpeg-src")

file(MAKE_DIRECTORY "${wiola_ffmpeg_prefix}/include")

set(wiola_ffmpeg_demuxers
    aac;aiff;ape;asf;au;caf;flac;matroska;mov;mp3;mpc;mpc8;ogg;tta;w64;wav;wv
)

set(wiola_ffmpeg_muxers flac;ipod;mov;mp3;mp4;ogg)

set(wiola_ffmpeg_decoders
    aac;alac;ape;flac;mp2;mp3;mpc7;mpc8;opus;tta;vorbis;wavpack;wmav1;wmav2
    pcm_alaw;pcm_f32be;pcm_f32le;pcm_f64be;pcm_f64le;pcm_mulaw;pcm_s8
    pcm_s16be;pcm_s16le;pcm_s24be;pcm_s24le;pcm_s32be;pcm_s32le;pcm_u8
    adpcm_ima_wav;adpcm_ms
)

set(wiola_ffmpeg_parsers aac;flac;mpegaudio;opus;vorbis)

set(wiola_ffmpeg_configure
    "${wiola_ffmpeg_source}/configure"
    --prefix="${wiola_ffmpeg_prefix}"
    --disable-autodetect
    --disable-avdevice
    --disable-avfilter
    --disable-debug
    --disable-doc
    --disable-everything
    --disable-gpl
    --disable-nonfree
    --disable-iconv
    --disable-network
    --disable-programs
    --disable-shared
    --disable-swscale
    --disable-x86asm
    --enable-static
    --enable-avcodec
    --enable-avformat
    --enable-avutil
    --enable-swresample
    --enable-protocol=file
)

foreach(component IN LISTS wiola_ffmpeg_demuxers)
    list(APPEND wiola_ffmpeg_configure "--enable-demuxer=${component}")
endforeach()

foreach(component IN LISTS wiola_ffmpeg_muxers)
    list(APPEND wiola_ffmpeg_configure "--enable-muxer=${component}")
endforeach()

foreach(component IN LISTS wiola_ffmpeg_decoders)
    list(APPEND wiola_ffmpeg_configure "--enable-decoder=${component}")
endforeach()

foreach(component IN LISTS wiola_ffmpeg_parsers)
    list(APPEND wiola_ffmpeg_configure "--enable-parser=${component}")
endforeach()

if(CMAKE_CROSSCOMPILING)
    if(NOT WIN32)
        message(
            FATAL_ERROR
            "The pinned FFmpeg recipe currently supports native builds and MinGW Windows"
        )
    endif()

    list(
        APPEND wiola_ffmpeg_configure
        --enable-cross-compile
        --target-os=mingw32
        --arch=x86_64
        "--cc=${CMAKE_C_COMPILER}"
        "--cross-prefix=x86_64-w64-mingw32-"
    )
endif()

ExternalProject_Add(
    wiola_ffmpeg_build
    URL https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz
    URL_HASH
        SHA256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/ffmpeg-download"
    SOURCE_DIR "${wiola_ffmpeg_source}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/ffmpeg-build"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CONFIGURE_COMMAND ${wiola_ffmpeg_configure}
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install
    BUILD_BYPRODUCTS
        "${wiola_ffmpeg_prefix}/lib/libavutil.a"
        "${wiola_ffmpeg_prefix}/lib/libavcodec.a"
        "${wiola_ffmpeg_prefix}/lib/libavformat.a"
        "${wiola_ffmpeg_prefix}/lib/libswresample.a"
)

function(wiola_ffmpeg_import library)
    add_library(wiola_ffmpeg_${library} STATIC IMPORTED GLOBAL)

    set_target_properties(
        wiola_ffmpeg_${library}
        PROPERTIES
            IMPORTED_LOCATION "${wiola_ffmpeg_prefix}/lib/lib${library}.a"
            INTERFACE_INCLUDE_DIRECTORIES "${wiola_ffmpeg_prefix}/include"
    )

    add_dependencies(wiola_ffmpeg_${library} wiola_ffmpeg_build)
endfunction()

foreach(library IN ITEMS avutil avcodec avformat swresample)
    wiola_ffmpeg_import(${library})
endforeach()

find_package(Threads REQUIRED)

add_library(wiola_ffmpeg INTERFACE)

target_link_libraries(
    wiola_ffmpeg
    INTERFACE
        wiola_ffmpeg_avformat
        wiola_ffmpeg_avcodec
        wiola_ffmpeg_swresample
        wiola_ffmpeg_avutil
        Threads::Threads
        $<$<NOT:$<PLATFORM_ID:Windows>>:m>
        $<$<PLATFORM_ID:Windows>:ws2_32>
        $<$<PLATFORM_ID:Windows>:bcrypt>
)
