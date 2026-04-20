/**
 * @file modbus_reader.hpp
 * @brief MODBUS/TCP client wrapper used by acquisition tasks.
 */

#pragma once

#include <string>
#include <cstdint>
#include <array>
#include <chrono>
#include <cstring>

namespace bms
{
    /**
     * @brief Connection, timeout, and retry settings for one MODBUS/TCP endpoint.
     */
    struct ModbusTcpConfig final
    {
        std::string host;
        int port{502};
        int unit_id{1};

        // Timeouts (sec + usec) for libmodbus API
        int response_timeout_sec{1};
        int response_timeout_usec{0};
        int byte_timeout_sec{0};
        int byte_timeout_usec{500000}; // 500ms

        // Retry policy
        int connect_retries{3};
        int read_retries{2};
    };

    /**
     * @brief Mutable counters and last-error fields for MODBUS operations.
     */
    struct ModbusStatus final
    {
        int last_errno{0};
        std::string last_error;
        std::uint64_t connect_failures{0};
        std::uint64_t read_failures{0};
        std::uint64_t reconnects{0};
        std::uint64_t successful_reads{0};
    };

    // All MODBUS constants centralized here for single source of truth
    inline constexpr int kModbusStartAddr = 3;             // First register address
    inline constexpr std::size_t kRegisterBlockCount = 35; // Registers 3-37 (35 total)
    inline constexpr std::size_t kChannelCount = 16;       // Voltage/temperature channels

    /**
     * @brief Thin libmodbus wrapper with reconnect and retry behavior.
     */
    class ModbusTcpClient final
    {
    public:
        /**
         * @brief Creates a client with copied connection settings.
         * @param cfg Endpoint, timeout, and retry configuration.
         */
        explicit ModbusTcpClient(ModbusTcpConfig cfg);
        ~ModbusTcpClient();

        // Non-copyable
        ModbusTcpClient(const ModbusTcpClient &) = delete;
        ModbusTcpClient &operator=(const ModbusTcpClient &) = delete;

        /**
         * @brief Opens a MODBUS/TCP connection.
         * @return True when session setup succeeds.
         */
        bool connect();
        /**
         * @brief Closes and frees the active MODBUS context.
         */
        void disconnect();
        /**
         * @brief Checks whether a live context is currently available.
         */
        bool is_connected() const noexcept;

        /**
         * @brief Reads a contiguous register block from the remote device.
         * @param addr Starting register address.
         * @param count Number of registers to read.
         * @param dest Destination buffer of at least @p count entries.
         * @return True on successful full-length read.
         */
        bool read_input_registers(int addr, int count, std::uint16_t *dest);

        /**
         * @brief Reads the canonical BMS block (registers 3..37) in one transaction.
         * @param out_regs Fixed-size destination buffer.
         * @return True on successful full block read.
         */
        bool read_bms_block(std::array<std::uint16_t, kRegisterBlockCount> &out_regs);

        /** @brief Updates response timeout (applied immediately when connected). */
        void set_response_timeout(std::chrono::milliseconds timeout);
        /** @brief Updates inter-byte timeout (applied immediately when connected). */
        void set_byte_timeout(std::chrono::milliseconds timeout);

        const ModbusTcpConfig &config() const noexcept { return cfg_; }
        const ModbusStatus &status() const noexcept { return status_; }

    private:
        bool ensure_connected_();
        void update_error_(const char *prefix);

        ModbusTcpConfig cfg_;
        ModbusStatus status_;
        void *ctx_{nullptr}; // Opaque modbus_t*
        bool connected_{false};
    };

    inline float modbus_registers_to_float(
        std::uint16_t high_word,
        std::uint16_t low_word) noexcept
    {
        const std::uint32_t raw =
            (static_cast<std::uint32_t>(high_word) << 16) |
            static_cast<std::uint32_t>(low_word);

        float result;
        std::memcpy(&result, &raw, sizeof(float));
        return result;
    }

} // namespace bms
