/**
 * @file        soh.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoH processing task scaffold (injectable estimator strategy).
 * @version     0.0.1
 * @date        2026-04-12
 */

#pragma once

#include "db_consumer.hpp"
#include "estimators.hpp"

#include <boost/atomic.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace bms
{
    /**
     * @brief Configuration for periodic SoH row-consumer behavior.
     */
    struct SoHTaskConfig final
    {
        std::uint64_t initial_expected_cursor{1};
    };

    /**
     * @brief SoH task diagnostics.
     */
    struct SoHTaskDiagnostics final
    {
        boost::atomic<std::uint64_t> rows_processed{0};
        boost::atomic<std::uint64_t> duplicates_skipped{0};
        boost::atomic<std::uint64_t> out_of_order_rows{0};
        boost::atomic<std::uint64_t> processing_failures{0};
        boost::atomic<std::uint64_t> estimator_rejections{0};
        boost::atomic<std::uint64_t> last_processed_cursor{0};
        boost::atomic<std::int64_t> last_latency_ms{0};
        boost::atomic<std::int64_t> last_estimated_soh_milli_pct{-1};
        std::string last_status{};
        std::string last_estimator_message{};
    };

    /**
     * @brief Row-by-row SoH processing task with injectable estimator strategy.
     */
    class SoHTask final
    {
    public:
        using RowQueue = DBConsumerTask::RowQueue;

        SoHTask(SoHTaskConfig cfg,
                RowQueue &input_queue,
                std::shared_ptr<ISoHEstimator> estimator = std::make_shared<NoOpSoHEstimator>());

        SoHTask(const SoHTask &) = delete;
        SoHTask &operator=(const SoHTask &) = delete;

        /** @brief Periodic work loop entry-point. */
        void operator()();

        /**
         * @brief Delegates row processing to the configured estimator strategy.
         * @param row Ordered telemetry row.
         * @return Estimator result for the input row.
         */
        SoHEstimateResult process_row(const TelemetryRow &row);

        /** @brief Access task diagnostics. */
        const SoHTaskDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        SoHTaskConfig cfg_;
        RowQueue &input_queue_;
        std::shared_ptr<ISoHEstimator> estimator_;
        std::uint64_t expected_cursor_{1};
        SoHTaskDiagnostics diag_{};
    };

} // namespace bms
