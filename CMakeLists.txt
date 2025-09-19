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

# include(CTest)
# include(Catch)
# enable_testing()
# find_package(Catch2 REQUIRED)
# add_subdirectory(tests)

# ------------------------------------ SUBDIRECTORIES ------------------------------------ #
add_subdirectory(app/src/services)
add_subdirectory(app)
