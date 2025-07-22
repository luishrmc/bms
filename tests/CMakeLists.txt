add_executable(UnitTestMQTT test_mqtt.cc)
target_link_libraries(UnitTestMQTT PRIVATE
    mqtt_service_lib
    Catch2::Catch2WithMain
)

catch_discover_tests(UnitTestMQTT)

# add_executable(UnitTestModBus test_modbus.cc)
# target_link_libraries(UnitTestModBus PRIVATE
#     modbus_service_lib
#     Catch2::Catch2WithMain
# )

# catch_discover_tests(UnitTestModBus)
