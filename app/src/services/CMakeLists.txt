# --------------------------- DATA LOGGER LIBRARY --------------------------- #

# Create a static or shared library target for reusable data logger logic
add_library(data_logger_service_lib
    data_logger_service.cpp
)

target_link_libraries(data_logger_service_lib PUBLIC
    fmt::fmt
)

target_include_directories(data_logger_service_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/app/inc/services          
    ${PROJECT_SOURCE_DIR}/lib/modbuspp          
    ${CMAKE_BINARY_DIR}/app_config
)

# --------------------------- INFLUXDB LIBRARY --------------------------- #

add_library(influxdb_service_lib
    influxdb_service.cpp
)

target_link_libraries(influxdb_service_lib PUBLIC
    curl
)

target_include_directories(influxdb_service_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/app/inc/services          
    ${CMAKE_BINARY_DIR}/app_config
)
