cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ------------------------------------ PROJECT DEFINITIONS ------------------------------------ #
project(
    bms
    VERSION 0.0.1
    LANGUAGES C CXX
)

# ------------------------------------ CPP/C CONFIG ------------------------------------ #
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ------------------------------------ SYSTEM CONFIG ------------------------------------ #
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Get the current Git commit hash
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file(
    "config/config.hpp.in" "${CMAKE_BINARY_DIR}/app_config/config.hpp"
    ESCAPE_QUOTES)

# ------------------------------------ CCACHE SUPPORT ------------------------------------ #
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    message(STATUS "Using ccache")
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

# ------------------------------------ DEPENDENCIES ------------------------------------ #
# CMake Package Manager
include(CPM)

# Paho C++ + bundled Paho C
CPMAddPackage(
  NAME paho-mqttpp3
  GITHUB_REPOSITORY eclipse-paho/paho.mqtt.cpp
  GIT_TAG v1.5.3

  OPTIONS
    "PAHO_WITH_MQTT_C ON"          # build bundled C lib; avoids find_package failure
    "PAHO_BUILD_STATIC ON"         # produce static lib (good for embedded)
    "PAHO_BUILD_SHARED OFF"
    "PAHO_WITH_SSL OFF"
    "PAHO_BUILD_EXAMPLES OFF"
    "PAHO_BUILD_SAMPLES OFF"
    "PAHO_BUILD_TESTS OFF"
)

# ------------------------------------ REQUIREMENTS ------------------------------------ #
find_package(Boost REQUIRED COMPONENTS system thread chrono)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBMODBUS REQUIRED IMPORTED_TARGET libmodbus)
add_library(libmodbus::modbus INTERFACE IMPORTED)
target_link_libraries(libmodbus::modbus INTERFACE PkgConfig::LIBMODBUS)

# ------------------------------------ SUBDIRECTORIES ------------------------------------ #
add_subdirectory(app)
