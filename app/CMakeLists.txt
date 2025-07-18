# --------------------------- APP CONFIGURATION --------------------------- #

# Relative path to the local Modbus++ header-only library
set(LIB_MODBUS lib/modbuspp)

# Define the name of the final executable
set(BMS_EXEC_NAME bms)

set(CTRL src/controllers)
set(SRVC src/services)

# --------------------------- EXECUTABLE TARGET --------------------------- #

# Create the main application binary from the entry point
add_executable(${BMS_EXEC_NAME}
    ${CTRL}/main.cpp
    ${SRVC}/data_logger_service.cpp
)

# --------------------------- INCLUDE DIRECTORIES --------------------------- #

target_include_directories(${BMS_EXEC_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/${LIB_MODBUS}
    ${PROJECT_SOURCE_DIR}/config
    ${CMAKE_BINARY_DIR}/app_config

    inc/controllers
    inc/services
)

# --------------------------- COMPILE DEFINITIONS --------------------------- #

# Define compile-time macros for the target
target_compile_definitions(${BMS_EXEC_NAME} PUBLIC
    pthread
)

# --------------------------- LINKED LIBRARIES --------------------------- #

# Link against external libraries pulled via CPM
target_link_libraries(${BMS_EXEC_NAME} PRIVATE
    nlohmann_json::nlohmann_json
    PahoMqttCpp::paho-mqttpp3
    fmt::fmt
)

# --------------------------- OUTPUT DIRECTORY --------------------------- #

# Place the built executable in a clean `project_output` directory
# inside the root build tree (e.g., build/project_output/)
set_target_properties(${BMS_EXEC_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/project_output
)

