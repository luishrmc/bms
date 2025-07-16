cmake_minimum_required(VERSION 3.22)

project(
    BMS
    VERSION 0.0.1
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(EXECUTABLE_NAME "main")
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

include(CPM)

# nlohmann/json (re-enable; see note below)
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

# Build Paho C++ + bundled Paho C
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

add_executable(${EXECUTABLE_NAME}
    main.cpp
)

target_link_libraries(${EXECUTABLE_NAME} PRIVATE
    nlohmann_json::nlohmann_json
    PahoMqttCpp::paho-mqttpp3     # resolves to static or shared as built
)

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/project_output
)
