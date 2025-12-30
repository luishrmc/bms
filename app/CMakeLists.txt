# --------------------------- EXECUTABLE CONFIGURATION --------------------------- #

# Set the name of the final executable target
set(BMS_EXEC_NAME bms)

# Define the main executable and specify its source files
add_executable(${BMS_EXEC_NAME}
    src/main.cpp
    src/influxdb.cpp
    src/modbus_reader.cpp
)

# Add include directories needed for this target (e.g., generated config headers)
target_include_directories(${BMS_EXEC_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/config
    ${CMAKE_BINARY_DIR}/app_config
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

# Link required internal libraries to the executable
target_link_libraries(${BMS_EXEC_NAME} PRIVATE
    nlohmann_json::nlohmann_json       
    PahoMqttCpp::paho-mqttpp3 
    Boost::system
    Boost::thread
    Boost::chrono
    Threads::Threads
    libmodbus::modbus
    curl
)

# Define any required compile-time macros for the target
target_compile_definitions(${BMS_EXEC_NAME} PRIVATE
)

# --------------------------- OUTPUT DIRECTORY --------------------------- #

# Place the built executable in a structured 'project_output' directory
# within the build tree for easier discovery and organization
set_target_properties(${BMS_EXEC_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/project_output
)
