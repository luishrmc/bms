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
             "apiv3_n7_oUpwKZ7m2k_Y2qTK3UY3S3Py7CG8n8ZPuNz2zyAfL88Hsuu7Mok8KBG8MxJcjAM9NjPA6X3HKUE7ES5HZTA",
             "sample-air-sensor-1756042262622")
    {
    }

    ~influxdbFixture()
    {
    }
};

TEST_CASE_METHOD(influxdbFixture, "InfluxDB Connect", "[influxdb]")
{
    SECTION("Connect to InfluxDB")
    {
        REQUIRE(db.connect() == arrow::Status::OK());
    }
}

TEST_CASE_METHOD(influxdbFixture, "InfluxDB Insert", "[influxdb]")
{
    SECTION("Insert to InfluxDB")
    {
        std::string lp_line =
            "air,sensor_id=ENV002 "
            "co=0.56761,humidity=27.98,temperature=109.75,status=3i";

        REQUIRE(db.connect() == arrow::Status::OK());
        // REQUIRE(db.insert(lp_line) == arrow::Status::OK());
    }

    SECTION("Insert batch to InfluxDB")
    {
        std::vector<std::string> lines = {
            "air,sensor_id=ENV003 "
            "co=0.56762,humidity=27.98,temperature=109.75,status=3i",
            "air,sensor_id=ENV004 "
            "co=0.56763,humidity=27.98,temperature=109.75,status=3i",
            "air,sensor_id=ENV005 "
            "co=0.56764,humidity=27.98,temperature=109.75,status=3i",
            "air,sensor_id=ENV006 "
            "co=0.56765,humidity=27.98,temperature=109.75,status=3i",
            "air,sensor_id=ENV007 "
            "co=0.56767,humidity=27.98,temperature=109.75,status=3i"};

        REQUIRE(db.insert_batch(lines) == arrow::Status::OK());
    }
}
