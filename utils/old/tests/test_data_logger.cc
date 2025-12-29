#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "data_logger_service.hpp"
#include <bit>

using Catch::Approx;

struct DataLoggerFixture
{
    DataLoggerService dl;

    DataLoggerFixture()
        : dl("127.0.0.1", 5020, 1)
    {
        dl.connect(3).get();
    }

    ~DataLoggerFixture()
    {
        dl.disconnect();
    }
};

TEST_CASE_METHOD(DataLoggerFixture, "Data Logger Service Connection Test", "[data_logger][connection]")
{
    REQUIRE(dl.is_connected() == true);
}

TEST_CASE_METHOD(DataLoggerFixture, "Data Logger Status", "[data_logger]")
{
    REQUIRE(dl.read_status() == 0);
    REQUIRE(dl._mode == DataLoggerService::dl_mode_t::RUN);
    REQUIRE(dl._ntp == true);
    REQUIRE(dl._autocal == true);
}

TEST_CASE_METHOD(DataLoggerFixture, "Read Active Sampling", "[data_logger]")
{
    dl.read_act_sampling();
    REQUIRE(dl._sampling_period == 3000); // Replace with expected value
}

TEST_CASE_METHOD(DataLoggerFixture, "read_channel decodes float correctly", "[data_logger]")
{
    for (uint8_t ch = 0; ch < 16; ch++)
    {
        dl.read_channel(ch);
        REQUIRE(dl._adc_channels[ch] == Approx(1.25f + ch * 1.0f).epsilon(0.01f));
    }
}

TEST_CASE_METHOD(DataLoggerFixture, "read_board_temp read board temperature", "[data_logger]")
{
    REQUIRE(dl.read_board_temp() == 0);
    REQUIRE(dl._board_temp == 302); // 30.2
}

TEST_CASE_METHOD(DataLoggerFixture, "read_board_uid read board uid", "[data_logger]")
{
    REQUIRE(dl.read_board_uid() == 0);
    REQUIRE(dl._board_uid == 0xA30FD07C);
}

TEST_CASE_METHOD(DataLoggerFixture, "read_firmware_version read firmware version", "[data_logger]")
{
    REQUIRE(dl.read_firmware_version() == 0);
    REQUIRE(dl._fw_major == 8);
    REQUIRE(dl._fw_minor == 32);
    REQUIRE(dl._fw_build == 42);
}
