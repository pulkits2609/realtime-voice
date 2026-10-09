
cmake_minimum_required(VERSION 3.16)

# Optional vcpkg integration using VCPKG_ROOT
# The toolchain must be configured before project()

if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)

    if(DEFINED ENV{VCPKG_ROOT} AND
       NOT "$ENV{VCPKG_ROOT}" STREQUAL "")

        set(
            CMAKE_TOOLCHAIN_FILE
            "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "vcpkg toolchain"
        )

    endif()

endif()

project(realtime_voice
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# dependencies :

# Boost.Asio headers
# Search standard system paths, vcpkg and environment hints

find_path(
    BOOST_INCLUDE_DIR
    NAMES boost/asio.hpp
    HINTS
        ENV BOOST_INCLUDEDIR
        ENV BOOST_ROOT
    PATH_SUFFIXES
        include
)

if(NOT BOOST_INCLUDE_DIR)

    message(FATAL_ERROR
        "Boost.Asio headers not found. Install Boost or set BOOST_INCLUDEDIR."
    )

endif()

message(STATUS "Boost headers: ${BOOST_INCLUDE_DIR}")

# Threading support

find_package(Threads REQUIRED)

# Opus dependency
# Windows : CMake package (typically via vcpkg)
# Linux   : pkg-config

if(WIN32)

    find_package(Opus CONFIG REQUIRED)

    set(OPUS_TARGET Opus::opus)

else()

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(
        OPUS
        REQUIRED
        IMPORTED_TARGET
        opus
    )

    set(OPUS_TARGET PkgConfig::OPUS)

endif()

# VOICE SERVER

add_executable(voice_server

    server/main.cpp

    src/Network/server.cpp
    src/Network/udpSocket.cpp
    src/Network/packet.cpp

    src/Audio/miniaudio_impl.cpp

    src/Network/tcpControl.cpp
)

# VOICE CLIENT

add_executable(voice_client

    client/main.cpp

    src/Network/client.cpp
    src/Network/udpSocket.cpp
    src/Network/packet.cpp
    src/Network/jitterBuffer.cpp

    src/Audio/miniaudio_impl.cpp
    src/Audio/audioCapture.cpp
    src/Audio/audioPlayback.cpp
    src/Audio/pcmBuffer.cpp

    src/Codec/opusEncoder.cpp
    src/Codec/opusDecoder.cpp

    src/Network/tcpControl.cpp
)

# Debug configuration

target_compile_definitions(voice_client PRIVATE
    $<$<CONFIG:Debug>:VOICE_DEBUG>
)

target_compile_definitions(voice_server PRIVATE
    $<$<CONFIG:Debug>:VOICE_DEBUG>
)

# Common include directories

target_include_directories(voice_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)

target_include_directories(voice_client PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)

# Server libraries

target_link_libraries(voice_server PRIVATE
    Threads::Threads
)

# Client libraries

target_link_libraries(voice_client PRIVATE
    ${OPUS_TARGET}
    Threads::Threads
)

# Windows networking library

if(WIN32)

    target_link_libraries(voice_server PRIVATE
        ws2_32
    )

    target_link_libraries(voice_client PRIVATE
        ws2_32
    )

endif()

# Opus Test

add_executable(opus_test

    src/Tests/opusTest.cpp

    src/Codec/opusEncoder.cpp
    src/Codec/opusDecoder.cpp
)

target_compile_definitions(opus_test PRIVATE
    $<$<CONFIG:Debug>:VOICE_DEBUG>
)

target_include_directories(opus_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)

target_link_libraries(opus_test PRIVATE
    ${OPUS_TARGET}
)

# Voice Packet Test

add_executable(voice_packet_test

    src/Tests/voicePacketTest.cpp

    src/Network/packet.cpp
)

target_compile_definitions(voice_packet_test PRIVATE
    $<$<CONFIG:Debug>:VOICE_DEBUG>
)

target_include_directories(voice_packet_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)
