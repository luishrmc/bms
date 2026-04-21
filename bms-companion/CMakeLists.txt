# --------------------------- EXECUTABLE CONFIGURATION --------------------------- #

# Set the name of the final executable target
set(BMS_COMPANION_EXEC_NAME bms_companion)

# Define the main executable and specify its source files
add_executable(${BMS_COMPANION_EXEC_NAME}
    src/main.cpp
    src/modbus_codec.cpp
    src/rs485_task.cpp
    src/mqtt_task.cpp
)

# Add include directories needed for this target (e.g., generated config headers)
target_include_directories(${BMS_COMPANION_EXEC_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_BINARY_DIR}/app_config
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${PROJECT_SOURCE_DIR}/app/inc
)

# Link required internal libraries to the executable
target_link_libraries(${BMS_COMPANION_EXEC_NAME} PRIVATE
    nlohmann_json::nlohmann_json       
    Boost::system
    Boost::thread
    Boost::chrono
    Threads::Threads
    libmodbus::modbus
    paho-mqttpp3
    paho-mqtt3as
)

# Define any required compile-time macros for the target
target_compile_definitions(${BMS_COMPANION_EXEC_NAME} PRIVATE
)

# --------------------------- OUTPUT DIRECTORY --------------------------- #

# Place the built executable in a structured 'project_output' directory
# within the build tree for easier discovery and organization
set_target_properties(${BMS_COMPANION_EXEC_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)

# Set output directory based on build type
if(CMAKE_CROSSCOMPILING)
    set(OUTPUT_DIR "${PROJECT_SOURCE_DIR}/rasp")
else()
    set(OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin")
endif()

set_target_properties(${BMS_COMPANION_EXEC_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
)
