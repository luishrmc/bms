#pragma once

#include "batch_structures.hpp"
#include "modbus_reader.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace bms
{
    struct TemperatureConsoleAcquisitionConfig final
    {
        ModbusTcpConfig device{};
        std::uint64_t diagnostics_every_cycles{0};
    };

    struct TemperatureConsoleDiagnostics final
    {
        std::atomic<std::uint64_t> attempts{0};
        std::atomic<std::uint64_t> successes{0};
        std::atomic<std::uint64_t> failures{0};
        std::atomic<std::int64_t> last_cycle_duration_ms{0};
    };

    class TemperatureConsoleAcquisition final
    {
    public:
        explicit TemperatureConsoleAcquisition(TemperatureConsoleAcquisitionConfig cfg);

        TemperatureConsoleAcquisition(const TemperatureConsoleAcquisition &) = delete;
        TemperatureConsoleAcquisition &operator=(const TemperatureConsoleAcquisition &) = delete;

        bool connect();
        void disconnect();

        void operator()();

        const TemperatureConsoleAcquisitionConfig &config() const noexcept { return cfg_; }
        const TemperatureConsoleDiagnostics &diagnostics() const noexcept { return diagnostics_; }
        const ModbusStatus &device_status() const noexcept { return device_.status(); }

    private:
        static float decode_channel_(const std::array<std::uint16_t, kRegisterBlockCount> &regs,
                                     std::size_t channel_index) noexcept;
        static std::string format_timestamp_(std::chrono::system_clock::time_point tp);

        void log_success_(const std::array<float, kChannelCount> &temperatures,
                          std::chrono::system_clock::time_point ts);
        void log_failure_();
        void log_diagnostics_();

        TemperatureConsoleAcquisitionConfig cfg_;
        ModbusTcpClient device_;

        TemperatureConsoleDiagnostics diagnostics_{};
        std::uint64_t sequence_{0};
    };

} // namespace bms
