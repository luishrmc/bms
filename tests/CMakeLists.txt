# add_executable(UnitTestDataLogger test_data_logger.cc)
# target_link_libraries(UnitTestDataLogger PRIVATE
#     data_logger_service_lib
#     Catch2::Catch2WithMain
# )

# catch_discover_tests(UnitTestDataLogger)

add_executable(UnitTestInfluxdb
    test_influxdb.cc
)

target_link_libraries(UnitTestInfluxdb PRIVATE
    influxdb_service_lib
    Catch2::Catch2WithMain
)

catch_discover_tests(UnitTestInfluxdb)
