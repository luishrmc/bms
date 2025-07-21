# --------------------------- MODBUS LIBRARY --------------------------- #

# Create a static or shared library target for reusable modbus logic
add_library(modbus_service_lib
    modbus_service.cpp
)

# Link external dependencies required by the modbus_service_lib
target_link_libraries(modbus_service_lib PUBLIC
)

# Specify public include directories to expose to any target linking this library
target_include_directories(modbus_service_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/lib/modbuspp              # Custom Modbus++ wrapper
    ${PROJECT_SOURCE_DIR}/app/inc/services          # Service-layer interfaces
)

# --------------------------- DATA LOGGER LIBRARY --------------------------- #

# Create a static or shared library target for reusable data logger logic
add_library(data_logger_service_lib
    data_logger_service.cpp
)

# Link external dependencies required by the data_logger_service_lib
target_link_libraries(data_logger_service_lib PUBLIC
    modbus_service_lib                   # For Modbus communication
)

target_include_directories(data_logger_service_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/app/inc/services          # Service-layer interfaces
)

# --------------------------- MQTT LIBRARY --------------------------- #

# Create a static or shared library target for reusable mqtt logic
add_library(mqtt_service_lib
    mqtt_service.cpp
)

# Link external dependencies required by the mqtt_service_lib
target_link_libraries(mqtt_service_lib PUBLIC
    fmt::fmt                           # For safe and efficient string formatting
    nlohmann_json::nlohmann_json       # For JSON serialization/deserialization
    PahoMqttCpp::paho-mqttpp3          # For MQTT client functionality
)

# Specify public include directories to expose to any target linking this library
target_include_directories(mqtt_service_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/app/inc/services          # Service-layer interfaces
)
