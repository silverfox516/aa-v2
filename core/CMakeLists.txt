cmake_minimum_required(VERSION 3.14)
project(aauto-core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# standalone asio (header-only, no boost)
add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/asio)
target_compile_definitions(asio INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED)

# Find threading library for asio
find_package(Threads REQUIRED)
target_link_libraries(asio INTERFACE Threads::Threads)

# Core library
add_library(aauto-core STATIC
    src/utils/ProtocolConstants.cpp
    src/utils/Logger.cpp
    src/session/Framer.cpp
    src/session/OutboundMessageEncoder.cpp
    src/session/InboundMessageAssembler.cpp
    src/session/HandshakeCoordinator.cpp
    src/session/Session.cpp
    src/service/VideoService.cpp
    src/service/AudioService.cpp
    src/service/InputService.cpp
    src/service/SensorService.cpp
    src/service/ControlService.cpp
    src/service/MicrophoneService.cpp
    src/service/NavigationStatusService.cpp
    src/service/PhoneStatusService.cpp
    src/service/MediaPlaybackService.cpp
    src/service/GenericNotificationService.cpp
    src/service/MediaBrowserService.cpp
    src/service/BluetoothService.cpp
    src/service/VendorExtensionService.cpp
    src/engine/Engine.cpp
    src/crypto/AapKeys.cpp
)

target_include_directories(aauto-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(aauto-core PUBLIC asio aap_protobuf)

# Mirror core/Android.bp warning set so the host CMake build matches the
# device build. -Wpedantic is intentionally NOT used: external headers
# (asio, abseil via protobuf) rely on compiler extensions like __int128
# which would trip pedantic and cannot be fixed upstream.
target_compile_options(aauto-core PRIVATE
    -Wall -Wextra -Werror
    -Wno-implicit-fallthrough)

# Clang-only suppressions: -Wunused-private-field exists only in clang;
# the deprecated-literal-operator/gcc-compat warnings are emitted only by
# newer clang inside external headers (asio, abseil).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(aauto-core PRIVATE
        -Wno-unused-private-field
        -Wno-error=deprecated-literal-operator
        -Wno-error=gcc-compat)
endif()
