cmake_minimum_required(VERSION 3.16)

project(realtime_voice
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS system)
find_package(PkgConfig REQUIRED)

pkg_check_modules(OPUS REQUIRED opus)

add_executable(voice_server
    server/main.cpp
    src/Network/server.cpp
    src/Audio/miniaudio_impl.cpp
    src/Network/udpSocket.cpp
    src/Network/packet.cpp
)

add_executable(voice_client
    client/main.cpp
    src/Audio/miniaudio_impl.cpp
    src/Network/client.cpp
    src/Network/udpSocket.cpp
    src/Network/packet.cpp
    src/Audio/audioCapture.cpp
    src/Audio/pcmBuffer.cpp
    src/Codec/opusEncoder.cpp
    src/Codec/opusDecoder.cpp
)

target_include_directories(voice_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
)

target_include_directories(voice_client PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
)

target_link_libraries(voice_server PRIVATE
    Boost::system
    ${OPUS_LIBRARIES}
)

target_link_libraries(voice_client PRIVATE
    Boost::system
    ${OPUS_LIBRARIES}
)

# Opus tests
add_executable(opus_test
    src/Tests/opusTest.cpp

    src/Codec/opusEncoder.cpp
    src/Codec/opusDecoder.cpp
)

target_include_directories(opus_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${OPUS_INCLUDE_DIRS}
)

target_link_libraries(opus_test PRIVATE
    ${OPUS_LIBRARIES}
)