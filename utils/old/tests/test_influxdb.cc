#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "influxdb_service.hpp"
#include "logging_service.hpp"
#include <bit>

struct influxdbFixture
{
    InfluxDBService db;

    influxdbFixture()
        : db("influxdb3-core",
             8181,
             "apiv3_V-9PSiCe_7cdb2R_GOyLVuvGaa6aelFGP0pdCNIGK6cMrpi1049a0qta5mmdrqCEtse0Aqc4Uz1BCgnsmI94dw",
             "voltage")
    {
    }

    ~influxdbFixture()
    {
    }
};
