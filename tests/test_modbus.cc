#include <catch2/catch_test_macros.hpp>
#include "data_logger_service.hpp"

TEST_CASE("DataLoggerService connects and reads successfully", "[modbus]") {

    DataLoggerService dl("127.0.0.1", 5020, 1);

    REQUIRE_NOTHROW(dl.connect());

    dl.disconnect();
}

