/**
 * @file        soh.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoH processing task scaffold (injectable estimator strategy).
 * @version     0.0.1
 * @date        2026-04-12
 */

#pragma once

#include "temperature.hpp"
#include "voltage_current.hpp"
#include "safe_queue.hpp"

#include <cstdint>
#include <optional>

namespace bms
{
    /**
     * @brief Runtime options for the SoH interface task.
     */
    struct SoHTaskConfig final
    {
        bool enable_diagnostics_logging{true};
    };

    /**
     * @brief Counters tracking SoH input stream alignment and progression.
     */
    struct SoHTaskDiagnostics final
    {
        std::uint64_t frames_observed{0};
        std::uint64_t frames_with_both_measurements{0};
        std::uint64_t last_voltage_sequence{0};
        std::uint64_t last_temperature_sequence{0};
    };

    /**
     * @brief Queue consumer that aligns voltage/current frames with latest temperatures.
     */
    class SoHTask final
    {
    public:
        using VoltageQueue = SafeQueue<VoltageCurrentSample>;
        using TemperatureQueue = SafeQueue<TemperatureSample>;

        /**
         * @brief Creates the SoH task bound to queue inputs.
         */
        SoHTask(SoHTaskConfig cfg, VoltageQueue &voltage_queue, TemperatureQueue &temperature_queue);

        SoHTask(const SoHTask &) = delete;
        SoHTask &operator=(const SoHTask &) = delete;

        /**
         * @brief Runs consumption loop until both queues are closed.
         */
        void operator()();

        const SoHTaskDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        SoHTaskConfig cfg_;
        VoltageQueue &voltage_queue_;
        TemperatureQueue &temperature_queue_;
        std::optional<TemperatureSample> latest_temperature_{};
        SoHTaskDiagnostics diag_{};
    };

} // namespace bms
