cmake_minimum_required(VERSION 3.20)
project(iap2_harness C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenSSL REQUIRED)
find_package(MbedTLS 3 REQUIRED)
find_package(Threads REQUIRED)


set(CARPLAY_MEDIA_BACKEND "auto" CACHE STRING "Media backend: auto, macos, headless, custom")
set_property(CACHE CARPLAY_MEDIA_BACKEND PROPERTY STRINGS auto macos headless custom)
set(CARPLAY_CUSTOM_MEDIA_SOURCE "" CACHE FILEPATH "Custom source implementing platform::createMediaBackend")
set(CARPLAY_CUSTOM_MEDIA_LIBRARIES "" CACHE STRING "Libraries for a custom media backend")
set(CARPLAY_CUSTOM_MEDIA_INCLUDE_DIRS "" CACHE STRING "Include directories for a custom media backend")
set(CARPLAY_SELECTED_MEDIA_BACKEND "${CARPLAY_MEDIA_BACKEND}")
if(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "auto")
  if(APPLE)
    set(CARPLAY_SELECTED_MEDIA_BACKEND macos)
  else()
    set(CARPLAY_SELECTED_MEDIA_BACKEND headless)
  endif()
endif()
if(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "macos")
  if(NOT APPLE)
    message(FATAL_ERROR "macos media backend requires Apple frameworks; use headless or supply a custom backend")
  endif()
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(PLATFORM_MEDIA_SOURCES src/platform/macos/media_backend.cpp src/airplay/mac_video.mm src/airplay/mac_audio.mm)
elseif(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "headless")
  set(PLATFORM_MEDIA_SOURCES src/platform/headless/media_backend.cpp)
  message(STATUS "Headless backend: protocol/decryption only; no native video/audio/touch implementation")
elseif(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "custom")
  if(NOT EXISTS "${CARPLAY_CUSTOM_MEDIA_SOURCE}")
    message(FATAL_ERROR "custom media backend requires CARPLAY_CUSTOM_MEDIA_SOURCE")
  endif()
  set(PLATFORM_MEDIA_SOURCES "${CARPLAY_CUSTOM_MEDIA_SOURCE}")
else()
  message(FATAL_ERROR "Unknown CARPLAY_MEDIA_BACKEND: ${CARPLAY_MEDIA_BACKEND}")
endif()
option(CARPLAY_ENABLE_USB "Build the experimental libusb host transport" ${APPLE})
set(DEFAULT_MAC_HELPERS OFF)
if(APPLE AND CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "macos")
  set(DEFAULT_MAC_HELPERS ON)
endif()
option(CARPLAY_BUILD_MAC_HELPERS "Build macOS Swift Bluetooth helpers" ${DEFAULT_MAC_HELPERS})
message(STATUS "CarPlay media backend: ${CARPLAY_SELECTED_MEDIA_BACKEND}")

add_library(iap2_core
  src/mfi/mock_signer.cpp
  src/mfi/openssl_signer.cpp
  src/iap2/message.cpp
  src/iap2/control_messages.cpp
  src/iap2/identification.cpp
  src/iap2/wireless_control.cpp
  src/iap2/auth_session.cpp
  src/iap2/link.cpp
  src/airplay/auth_setup.cpp
  src/airplay/airplay_session.cpp
  src/airplay/pairing.cpp
  src/airplay/audio_stream.cpp
  src/airplay/screen_stream.cpp
  src/airplay/aux_channels.cpp
  src/airplay/video_bitstream.cpp
  src/platform/state_paths.cpp
  src/airplay/tlv.cpp
  src/airplay_crypto/airplay_crypto.cpp
  src/airplay_crypto/ed25519/src/add_scalar.c
  src/airplay_crypto/ed25519/src/fe.c
  src/airplay_crypto/ed25519/src/ge.c
  src/airplay_crypto/ed25519/src/key_exchange.c
  src/airplay_crypto/ed25519/src/keypair.c
  src/airplay_crypto/ed25519/src/sc.c
  src/airplay_crypto/ed25519/src/seed.c
  src/airplay_crypto/ed25519/src/sha512.c
  src/airplay_crypto/ed25519/src/sign.c
  src/airplay_crypto/ed25519/src/verify.c
  src/airplay_crypto/ed25519/src/x25519_raw.c
  src/transport/tcp_transport.cpp
  src/transport/bt_bridge_transport.cpp
  src/iap2/iap2_link.cpp
  src/iap2/link_handshake.cpp
)
target_include_directories(iap2_core PUBLIC include)
target_include_directories(iap2_core PUBLIC include/airplay_crypto)
target_include_directories(iap2_core PUBLIC src/airplay_crypto)
target_link_libraries(iap2_core PUBLIC OpenSSL::Crypto MbedTLS::mbedtls MbedTLS::mbedcrypto Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
  target_link_libraries(iap2_core PUBLIC socket)
endif()
add_library(iap2_platform ${PLATFORM_MEDIA_SOURCES})
target_link_libraries(iap2_platform PUBLIC iap2_core)
if(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "macos")
  target_link_libraries(iap2_platform PRIVATE
    "-framework AppKit" "-framework CoreImage" "-framework CoreVideo"
    "-framework CoreMedia" "-framework VideoToolbox" "-framework AudioToolbox"
    "-framework Foundation")
elseif(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "custom")
  target_include_directories(iap2_platform PRIVATE ${CARPLAY_CUSTOM_MEDIA_INCLUDE_DIRS})
  target_link_libraries(iap2_platform PRIVATE ${CARPLAY_CUSTOM_MEDIA_LIBRARIES})
endif()
target_compile_options(iap2_core PRIVATE -Wall -Wextra -Wpedantic)


if(CARPLAY_ENABLE_USB)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
  add_library(iap2_transport src/transport/libusb_transport.cpp)
  target_link_libraries(iap2_transport PUBLIC iap2_core PkgConfig::LIBUSB)
else()
  add_library(iap2_transport src/transport/usb_unavailable.cpp)
  target_link_libraries(iap2_transport PUBLIC iap2_core)
endif()

add_executable(iap2-harness src/main.cpp)
target_link_libraries(iap2-harness PRIVATE iap2_platform iap2_transport)

include(CTest)
if(BUILD_TESTING)
  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE iap2_core)
  target_include_directories(core_tests PRIVATE tests)
  add_test(NAME core_tests COMMAND core_tests)
  add_test(NAME platform_info COMMAND iap2-harness --platform-info)
  if(CARPLAY_SELECTED_MEDIA_BACKEND STREQUAL "custom")
    set_tests_properties(platform_info PROPERTIES PASS_REGULAR_EXPRESSION "media backend:")
  else()
    set_tests_properties(platform_info PROPERTIES PASS_REGULAR_EXPRESSION "media backend: ${CARPLAY_SELECTED_MEDIA_BACKEND}")
  endif()
endif()

# Native helpers are optional; headless/Linux/QNX builds never require Swift.
if(CARPLAY_BUILD_MAC_HELPERS)
  if(NOT APPLE)
    message(FATAL_ERROR "CARPLAY_BUILD_MAC_HELPERS requires macOS")
  endif()
  find_program(SWIFTC_EXECUTABLE swiftc REQUIRED)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bt-bridge"
    COMMAND "${SWIFTC_EXECUTABLE}"
      -module-cache-path "${CMAKE_CURRENT_BINARY_DIR}/SwiftBridgeModuleCache"
      -O -o "${CMAKE_CURRENT_BINARY_DIR}/bt-bridge"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/playport-bt-bridge/main.swift"
      -framework IOBluetooth
    DEPENDS third_party/playport-bt-bridge/main.swift
    VERBATIM)
  add_custom_target(bluetooth_bridge ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bt-bridge")

  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bt-accessory"
    COMMAND "${SWIFTC_EXECUTABLE}"
      -module-cache-path "${CMAKE_CURRENT_BINARY_DIR}/SwiftModuleCache"
      -O -o "${CMAKE_CURRENT_BINARY_DIR}/bt-accessory"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bt-accessory.swift"
      -framework IOBluetooth
    DEPENDS scripts/bt-accessory.swift
    VERBATIM)
  add_custom_target(bluetooth_accessory ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bt-accessory")
endif()
