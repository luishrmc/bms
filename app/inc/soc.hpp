/**
 * @file        soc.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoC processing task scaffold (injectable estimator strategy).
 * @version     0.0.1
 * @date        2026-04-12
 */

#pragma once

#include "measurement_bus.hpp"

#include <cstdint>

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
        SoCTask(SoCTaskConfig cfg, const MeasurementBus &input_bus);

        SoCTask(const SoCTask &) = delete;
        SoCTask &operator=(const SoCTask &) = delete;

        void operator()();
        const SoCTaskDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        SoCTaskConfig cfg_;
        const MeasurementBus &input_bus_;
        std::uint64_t last_seen_voltage_sequence_{0};
        std::uint64_t last_seen_temperature_sequence_{0};
        SoCTaskDiagnostics diag_{};
    };

} // namespace bms
