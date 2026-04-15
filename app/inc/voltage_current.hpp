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
    struct VoltageCurrentAcquisitionConfig final
    {
        ModbusTcpConfig device1{};
        ModbusTcpConfig device2{};
        std::uint64_t diagnostics_every_cycles{50};
    };

    struct VoltageCurrentAcquisitionDiagnostics final
    {
        std::atomic<std::uint64_t> pair_attempts{0};
        std::atomic<std::uint64_t> pair_successes{0};
        std::atomic<std::uint64_t> pair_failures{0};

        std::atomic<std::uint64_t> device1_successes{0};
        std::atomic<std::uint64_t> device1_failures{0};
        std::atomic<std::uint64_t> device2_successes{0};
        std::atomic<std::uint64_t> device2_failures{0};

        std::atomic<std::int64_t> last_cycle_duration_ms{0};
    };

    class VoltageCurrentAcquisition final
    {
    public:
        explicit VoltageCurrentAcquisition(VoltageCurrentAcquisitionConfig cfg);

        VoltageCurrentAcquisition(const VoltageCurrentAcquisition &) = delete;
        VoltageCurrentAcquisition &operator=(const VoltageCurrentAcquisition &) = delete;

        bool connect();
        void disconnect();

        void operator()();

        const VoltageCurrentAcquisitionConfig &config() const noexcept { return cfg_; }
        const VoltageCurrentAcquisitionDiagnostics &diagnostics() const noexcept { return diagnostics_; }

        const ModbusStatus &device1_status() const noexcept { return dev1_.status(); }
        const ModbusStatus &device2_status() const noexcept { return dev2_.status(); }

    private:
        static float decode_channel_(const std::array<std::uint16_t, kRegisterBlockCount> &regs,
                                     std::size_t channel_index) noexcept;

        static std::string format_timestamp_(std::chrono::system_clock::time_point tp);

        void log_success_(const VoltageCurrentSample &sample);
        void log_failure_(bool dev1_ok, bool dev2_ok);
        void log_diagnostics_();

        VoltageCurrentAcquisitionConfig cfg_;

        ModbusTcpClient dev1_;
        ModbusTcpClient dev2_;

        VoltageCurrentAcquisitionDiagnostics diagnostics_{};
        std::uint64_t sequence_{0};
    };

} // namespace bms
