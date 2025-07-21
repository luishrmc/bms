# --------------------------- EXECUTABLE CONFIGURATION --------------------------- #

# Set the name of the final executable target
set(BMS_EXEC_NAME bms)

# Define the path to the controller source files
set(CTRL src/controllers)

# Define the main executable and specify its source files
add_executable(${BMS_EXEC_NAME}
    ${CTRL}/main.cpp
)

# Add include directories needed for this target (e.g., generated config headers)
target_include_directories(${BMS_EXEC_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/app_config
    ${PROJECT_SOURCE_DIR}/config                  # Project-wide configuration headers
)

# Link required internal libraries to the executable
target_link_libraries(${BMS_EXEC_NAME} PRIVATE
    modbus_service_lib
    mqtt_service_lib
)

# Define any required compile-time macros for the target
target_compile_definitions(${BMS_EXEC_NAME} PRIVATE
    pthread
)

# --------------------------- OUTPUT DIRECTORY --------------------------- #

# Place the built executable in a structured 'project_output' directory
# within the build tree for easier discovery and organization
set_target_properties(${BMS_EXEC_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/project_output
)
