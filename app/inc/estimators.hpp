/**
 * @file        estimators.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Estimator strategy interfaces for SoC/SoH task processing.
 * @version     0.0.1
 * @date        2026-04-14
 */

#pragma once

#include "db_consumer.hpp"

#include <optional>
#include <string>

namespace bms
{
    struct SoCEstimateResult final
    {
        bool accepted{false};
        std::string message{};
        std::optional<double> soc_percent{};
    };

    struct SoHEstimateResult final
    {
        bool accepted{false};
        std::string message{};
        std::optional<double> soh_percent{};
    };

    class ISoCEstimator
    {
    public:
        virtual ~ISoCEstimator() = default;
        virtual SoCEstimateResult estimate(const TelemetryRow &row) = 0;
    };

    class ISoHEstimator
    {
    public:
        virtual ~ISoHEstimator() = default;
        virtual SoHEstimateResult estimate(const TelemetryRow &row) = 0;
    };

    class NoOpSoCEstimator final : public ISoCEstimator
    {
    public:
        SoCEstimateResult estimate(const TelemetryRow &row) override
        {
            (void)row;
            return SoCEstimateResult{true, "no-op estimator accepted row", std::nullopt};
        }
    };

    class NoOpSoHEstimator final : public ISoHEstimator
    {
    public:
        SoHEstimateResult estimate(const TelemetryRow &row) override
        {
            (void)row;
            return SoHEstimateResult{true, "no-op estimator accepted row", std::nullopt};
        }
    };

} // namespace bms
