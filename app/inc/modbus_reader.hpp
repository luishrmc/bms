// inc/modbus_reader.hpp
#pragma once

#include "batch_structures.hpp"

#include <string>
#include <cstdint>
#include <array>
#include <chrono>

namespace bms
{
    /**
     * ModbusTcpConfig - MODBUS/TCP connection configuration
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
     *  ModbusStatus - Connection and read statistics
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

    /**
     * ModbusTcpClient - libmodbus wrapper for BMS acquisition
     */
    class ModbusTcpClient final
    {
    public:
        explicit ModbusTcpClient(ModbusTcpConfig cfg);
        ~ModbusTcpClient();

        // Non-copyable
        ModbusTcpClient(const ModbusTcpClient &) = delete;
        ModbusTcpClient &operator=(const ModbusTcpClient &) = delete;

        bool connect();
        void disconnect();
        bool is_connected() const noexcept;

        bool read_holding_registers(int addr, int count, std::uint16_t *dest);

        /**
         * Read BMS register block (3-37) in single transaction.
         * Returns type-safe array for population functions.
         */
        bool read_bms_block(std::array<std::uint16_t, kRegisterBlockCount> &out_regs);

        /**
         * High-level batch read (MODBUS + population + error mapping).
         */
        SampleFlags read_voltage_batch(VoltageBatch &batch);
        SampleFlags read_temperature_batch(TemperatureBatch &batch);

        void set_response_timeout(std::chrono::milliseconds timeout);
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

} // namespace bms
