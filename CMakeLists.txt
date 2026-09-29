cmake_minimum_required(VERSION 3.16)

project(realtime_voice
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# dependencies :

# dependencies :

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# Boost 1.90 is installed manually at C:/Boost
# Boost.System is header-only, so no Boost library linking is required.

set(BOOST_INCLUDE_DIR
    "C:/Boost/include/boost-1_90"
)

if(NOT EXISTS "${BOOST_INCLUDE_DIR}/boost")
    message(FATAL_ERROR
        "Boost headers not found at ${BOOST_INCLUDE_DIR}"
    )
endif()

# Linux : Opus discovered through pkg config
# Windows : Opus discovered through CMake/vcpkg

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
)

# common include directories
target_include_directories(voice_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)

target_include_directories(voice_client PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)

# server libraries
target_link_libraries(
    voice_server PRIVATE

    ${OPUS_TARGET}
)

# client libraries
target_link_libraries(
    voice_client PRIVATE

    ${OPUS_TARGET}
)

# windows networking library
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

target_include_directories(opus_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)

target_link_libraries(opus_test PRIVATE
    ${OPUS_TARGET}
)

# voice packet test
add_executable(voice_packet_test

    src/Tests/voicePacketTest.cpp

    src/Network/packet.cpp
)

target_include_directories(voice_packet_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${BOOST_INCLUDE_DIR}
)