/**
 * @file        soc.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoC processing task scaffold (injectable estimator strategy).
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
     * @brief Configuration for periodic SoC row-consumer behavior.
     */
    struct SoCTaskConfig final
    {
        std::uint64_t initial_expected_cursor{1};
    };

    /**
     * @brief SoC task diagnostics.
     */
    struct SoCTaskDiagnostics final
    {
        boost::atomic<std::uint64_t> rows_processed{0};
        boost::atomic<std::uint64_t> duplicates_skipped{0};
        boost::atomic<std::uint64_t> out_of_order_rows{0};
        boost::atomic<std::uint64_t> processing_failures{0};
        boost::atomic<std::uint64_t> estimator_rejections{0};
        boost::atomic<std::uint64_t> last_processed_cursor{0};
        boost::atomic<std::int64_t> last_latency_ms{0};
        boost::atomic<std::int64_t> last_estimated_soc_milli_pct{-1};
        std::string last_status{};
        std::string last_estimator_message{};
    };

    /**
     * @brief Row-by-row SoC processing task with injectable estimator strategy.
     */
    class SoCTask final
    {
    public:
        using RowQueue = DBConsumerTask::RowQueue;

        SoCTask(SoCTaskConfig cfg,
                RowQueue &input_queue,
                std::shared_ptr<ISoCEstimator> estimator = std::make_shared<NoOpSoCEstimator>());

        SoCTask(const SoCTask &) = delete;
        SoCTask &operator=(const SoCTask &) = delete;

        /** @brief Periodic work loop entry-point. */
        void operator()();

        /**
         * @brief Delegates row processing to the configured estimator strategy.
         * @param row Ordered telemetry row.
         * @return Estimator result for the input row.
         */
        SoCEstimateResult process_row(const TelemetryRow &row);

        /** @brief Access task diagnostics. */
        const SoCTaskDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        SoCTaskConfig cfg_;
        RowQueue &input_queue_;
        std::shared_ptr<ISoCEstimator> estimator_;
        std::uint64_t expected_cursor_{1};
        SoCTaskDiagnostics diag_{};
    };

} // namespace bms
