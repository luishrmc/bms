#pragma once

#include "battery_snapshot.hpp"

#include <array>
#include <cstdint>
#include <string>

struct _modbus;
using modbus_t = struct _modbus;

namespace bms
{

    struct RS485Config
    {
        std::string device{"/dev/ttyUSB0"};
        int baudrate{19200};
        char parity{'N'};
        int data_bits{8};
        int stop_bits{1};
        int slave_id{1};

        int response_timeout_sec{0};
        int response_timeout_usec{300000};

        int read_start_address{0};
        int read_register_count{125};

        float current_scale_a_per_lsb{0.0F};
    };

    class ModbusCodec
    {
    public:
        explicit ModbusCodec(const RS485Config &cfg);
        ~ModbusCodec();

        ModbusCodec(const ModbusCodec &) = delete;
        ModbusCodec &operator=(const ModbusCodec &) = delete;

        bool connect();
        void disconnect();
        bool is_connected() const noexcept;

        bool read_snapshot(BatterySnapshot &out_snapshot, std::string &error_out);

    private:
        static int16_t as_int16(uint16_t raw) noexcept;
        static std::string decode_ascii_block(
            const std::array<uint16_t, BatterySnapshot::kRegisterCount> &regs,
            std::size_t start,
            std::size_t end);

        RS485Config cfg_;
        modbus_t *ctx_{nullptr};
    };

} // namespace bms