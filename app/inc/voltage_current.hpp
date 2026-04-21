/**
 * @file voltage_current.hpp
 * @brief Voltage/current acquisition task and related configuration/diagnostics types.
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
     * @brief Runtime configuration for the dual-device voltage/current acquisition path.
     * @note Device 1 contributes cells 1..8, while Device 2 contributes cells 9..15 and
     *       one raw current-sensor channel selected by @ref current_source_channel.
     */
    struct VoltageCurrentAcquisitionConfig final
    {
        ModbusTcpConfig device1{};
        ModbusTcpConfig device2{};
        std::size_t current_source_channel{7};
        float current_scale_a_per_v{1.0F};
        float current_offset_a{0.0F};
        bool enable_sample_logging{false};
        std::uint64_t diagnostics_every_cycles{0};
    };

    /**
     * @brief Unified downstream sample containing 15 cell voltages and pack current.
     */
    struct VoltageCurrentSample final
    {
        std::chrono::system_clock::time_point timestamp{};
        std::array<float, 15> cell_voltages{};
        float raw_current_sensor_v{0.0F};
        float current_a{0.0F};
        std::uint64_t sequence{0};
    };

    /**
     * @brief Converts raw sensor voltage (V) into pack current (A).
     */
    class CurrentConverter final
    {
    public:
        /**
         * @brief Constructs a linear current converter.
         * @param scale_a_per_v Multiplicative gain in amperes per volt.
         * @param offset_a Additive offset in amperes.
         */
        CurrentConverter(float scale_a_per_v, float offset_a) noexcept
            : scale_a_per_v_(scale_a_per_v), offset_a_(offset_a)
        {
        }

        /**
         * @brief Converts sensor voltage to current.
         * @param sensor_voltage_v Raw sensor reading in volts.
         * @return Converted current in amperes.
         */
        float to_current_a(float sensor_voltage_v) const noexcept
        {
            return (sensor_voltage_v * scale_a_per_v_) + offset_a_;
        }

    private:
        float scale_a_per_v_{1.0F};
        float offset_a_{0.0F};
    };

    /**
     * @brief Lock-free counters exported for runtime diagnostics.
     */
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

    /**
     * @brief Periodic acquisition functor for pack voltage and current samples.
     * @details The task reads one canonical MODBUS block from each configured device and
     * maps decoded channels into a single @ref VoltageCurrentSample.
     */
    class VoltageCurrentAcquisition final
    {
    public:
        /**
         * @brief Callback invoked when a complete sample is available.
         * @note The callback is executed on the acquisition thread.
         */
        using SampleCallback = std::function<void(const VoltageCurrentSample &)>;

        /**
         * @brief Builds an acquisition task from runtime configuration.
         * @param cfg Device endpoints, current conversion settings, and logging controls.
         */
        explicit VoltageCurrentAcquisition(VoltageCurrentAcquisitionConfig cfg);

        VoltageCurrentAcquisition(const VoltageCurrentAcquisition &) = delete;
        VoltageCurrentAcquisition &operator=(const VoltageCurrentAcquisition &) = delete;

        /**
         * @brief Connects both MODBUS/TCP devices.
         * @return True only if both device connections succeed.
         */
        bool connect();
        /**
         * @brief Disconnects both MODBUS/TCP devices.
         */
        void disconnect();

        /**
         * @brief Executes one acquisition cycle.
         * @note Intended for periodic invocation by @ref PeriodicTask.
         */
        void operator()();

        const VoltageCurrentAcquisitionConfig &config() const noexcept { return cfg_; }
        const VoltageCurrentAcquisitionDiagnostics &diagnostics() const noexcept { return diagnostics_; }

        const ModbusStatus &device1_status() const noexcept { return dev1_.status(); }
        const ModbusStatus &device2_status() const noexcept { return dev2_.status(); }
        /**
         * @brief Registers the callback for successfully decoded samples.
         * @param callback Consumer callback; replaced on each call.
         */
        void set_sample_callback(SampleCallback callback) { on_sample_ = std::move(callback); }

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
        CurrentConverter converter_;
        SampleCallback on_sample_{};

        VoltageCurrentAcquisitionDiagnostics diagnostics_{};
        std::uint64_t sequence_{0};
    };

} // namespace bms
