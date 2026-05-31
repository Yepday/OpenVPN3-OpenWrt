cmake_minimum_required(VERSION 3.16)
project(openvpn3-openwrt CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenVPN3 core is header-only; passed in from OpenWrt Makefile via
# -DOPENVPN3_CORE_DIR=$(CURDIR)/../../openvpn3
if(NOT DEFINED OPENVPN3_CORE_DIR)
    set(OPENVPN3_CORE_DIR "${CMAKE_SOURCE_DIR}/../openvpn3-core/openvpn3")
endif()
message(STATUS "openvpn3-core dir: ${OPENVPN3_CORE_DIR}")

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(XXHASH REQUIRED libxxhash)
# ubus / libubox / uci do NOT ship .pc files in OpenWrt SDK;
# locate them directly instead of relying on pkg-config.
find_path(UBUS_INCLUDE_DIRS libubus.h REQUIRED)
find_library(UBUS_LIBRARIES NAMES ubus REQUIRED)

find_path(UBOX_INCLUDE_DIRS libubox/blobmsg_json.h REQUIRED)
find_library(UBOX_LIBRARIES NAMES ubox REQUIRED)

find_path(UCI_INCLUDE_DIRS uci.h REQUIRED)
find_library(UCI_LIBRARIES NAMES uci REQUIRED)

message(STATUS "ubus: ${UBUS_LIBRARIES}  (includes: ${UBUS_INCLUDE_DIRS})")
message(STATUS "ubox: ${UBOX_LIBRARIES}  (includes: ${UBOX_INCLUDE_DIRS})")
message(STATUS "uci:  ${UCI_LIBRARIES}   (includes: ${UCI_INCLUDE_DIRS})")

# Asio (header-only, standalone) — installed by libasio into staging_dir/usr/include
find_path(ASIO_INCLUDE_DIR asio.hpp REQUIRED)
message(STATUS "asio include dir: ${ASIO_INCLUDE_DIR}")

add_executable(openvpn3d
    src/main.cpp
    src/ubus_interface.cpp
    src/uci_config.cpp
    # openvpn3-core non-header-only sources
    ${OPENVPN3_CORE_DIR}/openvpn/crypto/data_epoch.cpp
)

target_include_directories(openvpn3d PRIVATE
    ${OPENVPN3_CORE_DIR}
    ${ASIO_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${XXHASH_INCLUDE_DIRS}
    ${UBUS_INCLUDE_DIRS}
    ${UBOX_INCLUDE_DIRS}
    ${UCI_INCLUDE_DIRS}
)

target_compile_definitions(openvpn3d PRIVATE
    ASIO_STANDALONE          # Use Asio without Boost
    USE_ASIO
    USE_OPENSSL              # OpenSSL backend (sslchoose.hpp picks this up)
    OPENVPN_PLATFORM_LINUX
    OPENVPN_USE_SITNL        # Linux route management via rtnetlink
)

target_link_libraries(openvpn3d PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ${JSONCPP_LIBRARIES}
    ${FMT_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${XXHASH_LIBRARIES}
    ${UBUS_LIBRARIES}
    ${UBOX_LIBRARIES}
    ${UCI_LIBRARIES}
    pthread
)

install(TARGETS openvpn3d DESTINATION sbin)
