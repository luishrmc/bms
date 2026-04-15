#pragma once

#include "batch_structures.hpp"
#include "modbus_reader.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace bms
{
    struct TemperatureAcquisitionConfig final
    {
        ModbusTcpConfig device{};
        bool enable_sample_logging{true};
        std::uint64_t diagnostics_every_cycles{0};
    };

    struct TemperatureAcquisitionDiagnostics final
    {
        std::atomic<std::uint64_t> attempts{0};
        std::atomic<std::uint64_t> successes{0};
        std::atomic<std::uint64_t> failures{0};
        std::atomic<std::int64_t> last_cycle_duration_ms{0};
    };

    class TemperatureAcquisition final
    {
    public:
        using SampleCallback = std::function<void(const TemperatureSample &)>;

        explicit TemperatureAcquisition(TemperatureAcquisitionConfig cfg);

        TemperatureAcquisition(const TemperatureAcquisition &) = delete;
        TemperatureAcquisition &operator=(const TemperatureAcquisition &) = delete;

        bool connect();
        void disconnect();

        void operator()();

        const TemperatureAcquisitionConfig &config() const noexcept { return cfg_; }
        const TemperatureAcquisitionDiagnostics &diagnostics() const noexcept { return diagnostics_; }
        const ModbusStatus &device_status() const noexcept { return device_.status(); }
        void set_sample_callback(SampleCallback callback) { on_sample_ = std::move(callback); }

    private:
        static float decode_channel_(const std::array<std::uint16_t, kRegisterBlockCount> &regs,
                                     std::size_t channel_index) noexcept;
        static std::string format_timestamp_(std::chrono::system_clock::time_point tp);

        void log_success_(const TemperatureSample &sample);
        void log_failure_();
        void log_diagnostics_();

        TemperatureAcquisitionConfig cfg_;
        ModbusTcpClient device_;

        TemperatureAcquisitionDiagnostics diagnostics_{};
        std::uint64_t sequence_{0};
        SampleCallback on_sample_{};
    };

} // namespace bms
