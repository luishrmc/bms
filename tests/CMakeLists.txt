# add_executable(UnitTestMQTT test_mqtt.cc)
# target_link_libraries(UnitTestMQTT PRIVATE
#     mqtt_service_lib
#     Catch2::Catch2WithMain
# )

# catch_discover_tests(UnitTestMQTT)

add_executable(UnitTestDataLogger test_data_logger.cc)
target_link_libraries(UnitTestDataLogger PRIVATE
    data_logger_service_lib
    Catch2::Catch2WithMain
)

catch_discover_tests(UnitTestDataLogger)
