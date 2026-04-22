#pragma once

#include "battery_snapshot.hpp"

#include <array>
#include <cstdint>
#include <string>

struct _modbus;
using modbus_t = struct _modbus;

namespace bms
{

    /**
     * @brief RS485/Modbus RTU connection and read configuration.
     */
    struct RS485Config
    {
        /// Serial device path for libmodbus RTU.
        std::string device{"/dev/ttyUSB0"};
        int baudrate{19200};
        char parity{'N'};
        int data_bits{8};
        int stop_bits{1};
        int slave_id{1};

        int response_timeout_sec{0};
        int response_timeout_usec{300000};

        /// Start register for the battery poll.
        int read_start_address{0};
        /// Register count read on each poll.
        int read_register_count{125};

        /// Optional conversion scale for pack current (A/LSB).
        float current_scale_a_per_lsb{0.0F};
    };

    /**
     * @brief Handles Modbus RTU connectivity and battery register decoding.
     */
    class ModbusCodec
    {
    public:
        /**
         * @brief Creates a codec with fixed RS485/Modbus configuration.
         * @param cfg Serial and register mapping configuration.
         */
        explicit ModbusCodec(const RS485Config &cfg);
        ~ModbusCodec();

        ModbusCodec(const ModbusCodec &) = delete;
        ModbusCodec &operator=(const ModbusCodec &) = delete;

        /**
         * @brief Opens and configures the Modbus RTU connection.
         * @return True on success.
         */
        bool connect();
        /**
         * @brief Closes and releases the Modbus RTU context.
         */
        void disconnect();
        /**
         * @brief Indicates whether the codec currently owns an RTU context.
         * @return True when connected.
         */
        bool is_connected() const noexcept;

        /**
         * @brief Reads and decodes one battery snapshot.
         * @param out_snapshot Destination snapshot.
         * @param error_out Human-readable error detail on failure.
         * @return True when a snapshot was decoded successfully.
         * @warning The method expects the configured register count to match
         *          the battery map consumed by the runtime.
         */
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
