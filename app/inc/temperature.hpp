/**
 * @file temperature.hpp
 * @brief Temperature acquisition task and related configuration/diagnostics types.
 */

#pragma once

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
    /**
     * @brief Runtime configuration for single-device temperature acquisition.
     */
    struct TemperatureAcquisitionConfig final
    {
        ModbusTcpConfig device{};
        bool enable_sample_logging{false};
        std::uint64_t diagnostics_every_cycles{0};
    };

    /**
     * @brief Lock-free counters exported for temperature task diagnostics.
     */
    struct TemperatureAcquisitionDiagnostics final
    {
        std::atomic<std::uint64_t> attempts{0};
        std::atomic<std::uint64_t> successes{0};
        std::atomic<std::uint64_t> failures{0};
        std::atomic<std::int64_t> last_cycle_duration_ms{0};
    };

    /**
     * @brief Unified downstream sample containing 16 temperature channels.
     */
    struct TemperatureSample final
    {
        std::chrono::system_clock::time_point timestamp{};
        std::array<float, kChannelCount> temperatures{};
        std::uint64_t sequence{0};
    };

    /**
     * @brief Periodic acquisition functor for 16 temperature channels.
     */
    class TemperatureAcquisition final
    {
    public:
        /**
         * @brief Callback invoked when a complete temperature sample is available.
         */
        using SampleCallback = std::function<void(const TemperatureSample &)>;

        /**
         * @brief Builds the task from runtime configuration.
         * @param cfg Device endpoint and logging controls.
         */
        explicit TemperatureAcquisition(TemperatureAcquisitionConfig cfg);

        TemperatureAcquisition(const TemperatureAcquisition &) = delete;
        TemperatureAcquisition &operator=(const TemperatureAcquisition &) = delete;

        /**
         * @brief Connects to the MODBUS/TCP temperature device.
         * @return True when connection succeeds.
         */
        bool connect();
        /**
         * @brief Disconnects from the MODBUS/TCP temperature device.
         */
        void disconnect();

        /**
         * @brief Executes one acquisition cycle.
         * @note Intended for periodic invocation by @ref PeriodicTask.
         */
        void operator()();

        const TemperatureAcquisitionConfig &config() const noexcept { return cfg_; }
        const TemperatureAcquisitionDiagnostics &diagnostics() const noexcept { return diagnostics_; }
        const ModbusStatus &device_status() const noexcept { return device_.status(); }
        /**
         * @brief Registers the callback for successful sample delivery.
         * @param callback Consumer callback; replaced on each call.
         */
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
