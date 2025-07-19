# --------------------------- DATA LOGGER LIBRARY --------------------------- #

# Define the path to the custom Modbus++ library
set(LIB_MODBUS lib/modbuspp)

# Create a static or shared library target for reusable data logger logic
add_library(data_logger_service_lib
    data_logger_service.cpp
)

# Link external dependencies required by the data_logger_service_lib
target_link_libraries(data_logger_service_lib PUBLIC
    nlohmann_json::nlohmann_json       # For JSON serialization/deserialization
    PahoMqttCpp::paho-mqttpp3          # For MQTT client functionality
    fmt::fmt                           # For safe and efficient string formatting
)

# Specify public include directories to expose to any target linking this library
target_include_directories(data_logger_service_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/${LIB_MODBUS}           # Custom Modbus++ wrapper
    ${PROJECT_SOURCE_DIR}/config                  # Project-wide configuration headers
    ${PROJECT_SOURCE_DIR}/app/inc/controllers     # Controller interfaces
    ${PROJECT_SOURCE_DIR}/app/inc/services        # Service-layer interfaces
)

