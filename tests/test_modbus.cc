#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modbus_service.hpp"
#include <bit>

using Catch::Approx; // brings Approx into scope

TEST_CASE("ModBus Service Read")
{
    ModBusService modbus("127.0.0.1", 5020, 1);
    auto conn_ret = modbus.connect(5);
    REQUIRE(conn_ret.get() == true);

    uint16_t u16_value = 0;
    auto read_ret = modbus.read_u16(0x0000, u16_value);
    REQUIRE(read_ret.get() == true);
    REQUIRE(u16_value == 0xC001);

    uint32_t u32_value = 0;
    auto read_u32_ret = modbus.read_u32(0x0060, u32_value);
    REQUIRE(read_u32_ret.get() == true);
    REQUIRE(u32_value == 0xA30FD07C);

    float ch[16];
    for (uint16_t i = 0; i < 15; ++i)
    {
        auto read_f32_ret = modbus.read_f32(0x0006 + 2 * i, ch[i]);
        REQUIRE(read_f32_ret.get() == true);
        REQUIRE(ch[i] == Approx(1.25f + i * 1.0f).epsilon(0.0001));
    }

    uint16_t chnn[32];
    auto read_raw_ret = modbus.read_raw(0x0006, 32, chnn);
    REQUIRE(read_raw_ret.get() == true);

    REQUIRE(chnn[0] == 0x3fa0);
    REQUIRE(chnn[1] == 0x0000);
    REQUIRE(chnn[30] == 0x4182);
    REQUIRE(chnn[31] == 0x0000);

    for (uint16_t i = 0; i < 15; ++i)
    {
        uint32_t bits = (uint32_t(chnn[2 * i]) << 16) | chnn[2 * i + 1];
        float ch = std::bit_cast<float>(bits);
        REQUIRE(ch == Approx(1.25f + i * 1.0f).epsilon(0.0001));
    }

    auto disc_ret = modbus.disconnect();
    REQUIRE(disc_ret.get() == true);
}

TEST_CASE("ModBus Service Write")
{
    ModBusService modbus("127.0.0.1", 5020, 1);
    auto conn_ret = modbus.connect(5);
    REQUIRE(conn_ret.get() == true);

    SECTION("Write and Read Holding Register 16bits")
    {
        uint16_t u16_value_w = 0xA30F;
        auto write_ret = modbus.write_u16(0x0000, u16_value_w);
        REQUIRE(write_ret.get() == true);

        uint16_t u16_value_r = 0;
        auto read_ret = modbus.read_u16(0x0000, u16_value_r, ModBusService::reg_type_t::HOLDING);
        REQUIRE(read_ret.get() == true);
        REQUIRE(u16_value_w == u16_value_r);
    }

    SECTION("Write and Read Holding Register 32bits")
    {
        uint32_t u32_value_w = 0xA30FD07C;
        auto write_ret = modbus.write_u32(0x0008, u32_value_w);
        REQUIRE(write_ret.get() == true);

        uint32_t u32_value_r = 0;
        auto read_ret = modbus.read_u32(0x0008, u32_value_r, ModBusService::reg_type_t::HOLDING);
        REQUIRE(read_ret.get() == true);
        REQUIRE(u32_value_w == u32_value_r);
    }

    SECTION("Write and Read Holding Register float")
    {
        float f_value_w = 1.5;
        auto write_ret = modbus.write_f32(0x000A, f_value_w);
        REQUIRE(write_ret.get() == true);

        float f_value_r = 0;
        auto read_ret = modbus.read_f32(0x000A, f_value_r, ModBusService::reg_type_t::HOLDING);
        REQUIRE(read_ret.get() == true);
        REQUIRE(f_value_w == f_value_r);
    }

    auto disc_ret = modbus.disconnect();
    REQUIRE(disc_ret.get() == true);
}
