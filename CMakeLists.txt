cmake_minimum_required(VERSION 3.20)
project(nbt_carplay C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenSSL REQUIRED)
find_package(MbedTLS REQUIRED)


find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

if(APPLE)
  enable_language(OBJCXX)
  set(MAC_VIDEO_SOURCE src/airplay/mac_video.mm)
  set(MAC_AUDIO_SOURCE src/airplay/mac_audio.mm)
else()
  set(MAC_VIDEO_SOURCE src/airplay/mac_video_stub.cpp)
  set(MAC_AUDIO_SOURCE src/airplay/mac_audio_stub.cpp)
endif()

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
  ${MAC_VIDEO_SOURCE}
  ${MAC_AUDIO_SOURCE}
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
target_include_directories(iap2_core BEFORE PUBLIC /usr/local/opt/mbedtls@3/include)
target_include_directories(iap2_core PUBLIC include/airplay_crypto)
target_include_directories(iap2_core PUBLIC src/airplay_crypto)
target_link_libraries(iap2_core PUBLIC OpenSSL::Crypto MbedTLS::mbedtls MbedTLS::mbedcrypto)
if(APPLE)
  target_link_libraries(iap2_core PUBLIC
    "-framework AppKit" "-framework CoreImage" "-framework CoreVideo"
    "-framework CoreMedia" "-framework VideoToolbox" "-framework AudioToolbox"
    "-framework Foundation")
endif()
target_compile_options(iap2_core PRIVATE -Wall -Wextra -Wpedantic)


add_library(iap2_transport
  src/transport/libusb_transport.cpp
)
target_include_directories(iap2_transport PUBLIC include ${LIBUSB_INCLUDE_DIRS})
target_link_libraries(iap2_transport PUBLIC iap2_core ${LIBUSB_LIBRARIES})
target_link_directories(iap2_transport PUBLIC ${LIBUSB_LIBRARY_DIRS})

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE iap2_core)
target_include_directories(core_tests PRIVATE tests)
add_test(NAME core_tests COMMAND core_tests)

add_executable(iap2-harness src/main.cpp)
target_link_libraries(iap2-harness PRIVATE iap2_core iap2_transport iap2_core)

# Native Classic Bluetooth accessory probe and incoming stdio transport.
if(APPLE)
  find_program(SWIFTC_EXECUTABLE swiftc REQUIRED)
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
