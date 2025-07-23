cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

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

set(CMAKE_C_STANDARD 17)
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

# nlohmann/json
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.12.0

  OPTIONS
    "JSON_Install OFF"                 # don't install when vendored
    "JSON_MultipleHeaders ON"          # expand to full header tree (IDE friendly)
    "JSON_ImplicitConversions OFF"     # avoid surprising implicit json->T conversions
    "JSON_GlobalUDLs OFF"              # keep _json literal scoped (default; explicit here)
    "JSON_Diagnostics OFF"             # enable for debug builds if you want richer errors
    "JSON_Diagnostic_Positions OFF"    # enable if you want element position info
    "JSON_BuildTests OFF"              # skip upstream unit tests
)

# Paho C++ + bundled Paho C
CPMAddPackage(
  NAME paho-mqttpp3
  GITHUB_REPOSITORY eclipse-paho/paho.mqtt.cpp
  GIT_TAG v1.5.3

  OPTIONS
    "PAHO_WITH_MQTT_C ON"          # build bundled C lib; avoids find_package failure
    "PAHO_BUILD_STATIC ON"         # produce static lib (good for embedded)
    "PAHO_BUILD_SHARED OFF"
    "PAHO_WITH_SSL ON"             # toggle as needed
    "PAHO_BUILD_EXAMPLES OFF"
    "PAHO_BUILD_SAMPLES OFF"
    "PAHO_BUILD_TESTS OFF"
)

# FMT Library
CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 11.2.0
    OPTIONS
        "FMT_INSTALL OFF"
        "FMT_TEST OFF"
        "FMT_DOC OFF"
        "FMT_OS OFF"
)

# Catch2
CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.8.1
    OPTIONS
        "CATCH_INSTALL_DOCS OFF"
)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

include(CTest)
include(Catch)
enable_testing()
find_package(Catch2 REQUIRED)
add_subdirectory(tests)

# ------------------------------------ SUBDIRECTORIES ------------------------------------ #
add_subdirectory(app/src/services)
add_subdirectory(app)
