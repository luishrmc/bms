include(Catch)

add_executable(UnitTestModbus test_modbus.cc)
target_link_libraries(UnitTestModbus PRIVATE
    data_logger_service_lib
    Catch2::Catch2WithMain
)
catch_discover_tests(UnitTestModbus)
