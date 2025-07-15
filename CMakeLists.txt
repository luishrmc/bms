cmake_minimum_required(VERSION 3.22)

project(
    "BMS"
    VERSION 0.0.1
    LANGUAGES C CXX
)

# Global CMake variables are set here
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(EXECUTABLE_NAME "main")

add_executable(${EXECUTABLE_NAME} main.cpp)

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/project_output
)
