/**
 * @file        soh.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoH processing task scaffold (injectable estimator strategy).
 * @version     0.0.1
 * @date        2026-04-12
 */

#pragma once

#include "measurement_bus.hpp"

#include <cstdint>

namespace bms
{
    /**
     * @brief Configuration for periodic SoH row-consumer behavior.
     */
    struct SoHTaskConfig final
    {
        bool enable_diagnostics_logging{true};
    };

    /**
     * @brief SoH task diagnostics.
     */
    struct SoHTaskDiagnostics final
    {
        std::uint64_t frames_observed{0};
        std::uint64_t frames_with_both_measurements{0};
        std::uint64_t last_voltage_sequence{0};
        std::uint64_t last_temperature_sequence{0};
    };

    /**
     * @brief Row-by-row SoH processing task with injectable estimator strategy.
     */
    class SoHTask final
    {
    public:
        SoHTask(SoHTaskConfig cfg, const MeasurementBus &input_bus);

        SoHTask(const SoHTask &) = delete;
        SoHTask &operator=(const SoHTask &) = delete;

        /** @brief Periodic work loop entry-point. */
        void operator()();

        const SoHTaskDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        SoHTaskConfig cfg_;
        const MeasurementBus &input_bus_;
        std::uint64_t last_seen_voltage_sequence_{0};
        std::uint64_t last_seen_temperature_sequence_{0};
        SoHTaskDiagnostics diag_{};
    };

} // namespace bms
