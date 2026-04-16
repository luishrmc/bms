/**
 * @file        soc.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoC processing task scaffold (injectable estimator strategy).
 * @version     0.0.1
 * @date        2026-04-12
 */

#pragma once

#include "batch_structures.hpp"
#include "safe_queue.hpp"

#include <cstdint>
#include <optional>

namespace bms
{
    struct SoCTaskConfig final
    {
        bool enable_diagnostics_logging{true};
    };

    struct SoCTaskDiagnostics final
    {
        std::uint64_t frames_observed{0};
        std::uint64_t frames_with_both_measurements{0};
        std::uint64_t last_voltage_sequence{0};
        std::uint64_t last_temperature_sequence{0};
    };

    class SoCTask final
    {
    public:
        using VoltageQueue = SafeQueue<VoltageCurrentSample>;
        using TemperatureQueue = SafeQueue<TemperatureSample>;

        SoCTask(SoCTaskConfig cfg, VoltageQueue &voltage_queue, TemperatureQueue &temperature_queue);

        SoCTask(const SoCTask &) = delete;
        SoCTask &operator=(const SoCTask &) = delete;

        void operator()();
        const SoCTaskDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        SoCTaskConfig cfg_;
        VoltageQueue &voltage_queue_;
        TemperatureQueue &temperature_queue_;
        std::optional<TemperatureSample> latest_temperature_{};
        SoCTaskDiagnostics diag_{};
    };

} // namespace bms
