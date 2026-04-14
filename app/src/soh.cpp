/**
 * @file        soh.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoH task scaffold implementation.
 * @version     0.0.1
 * @date        2026-04-12
 */

#include "soh.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <utility>

namespace bms
{
    SoHTask::SoHTask(SoHTaskConfig cfg,
                     RowQueue &input_queue,
                     std::shared_ptr<ISoHEstimator> estimator)
        : cfg_(std::move(cfg)),
          input_queue_(input_queue),
          estimator_(estimator ? std::move(estimator) : std::make_shared<NoOpSoHEstimator>()),
          expected_cursor_(cfg_.initial_expected_cursor)
    {
    }

    void SoHTask::operator()()
    {
        TelemetryRow *row = nullptr;
        while (input_queue_.try_pop(row))
        {
            if (!row)
            {
                continue;
            }

            const TelemetryRow &sample = *row;

            if (sample.cursor < expected_cursor_)
            {
                diag_.duplicates_skipped.fetch_add(1);
                input_queue_.dispose(row);
                continue;
            }

            if (sample.cursor > expected_cursor_)
            {
                diag_.out_of_order_rows.fetch_add(1);
                std::cerr << "[SoH] Out-of-order cursor: expected " << expected_cursor_
                          << " got " << sample.cursor << std::endl;
                expected_cursor_ = sample.cursor;
            }

            const SoHEstimateResult estimate = process_row(sample);
            diag_.last_estimator_message = estimate.message;

            if (!estimate.accepted)
            {
                diag_.estimator_rejections.fetch_add(1);
                diag_.processing_failures.fetch_add(1);
                input_queue_.dispose(row);
                continue;
            }

            if (estimate.soh_percent.has_value())
            {
                const auto milli_pct = static_cast<std::int64_t>(std::llround(estimate.soh_percent.value() * 1000.0));
                diag_.last_estimated_soh_milli_pct.store(milli_pct);
            }

            diag_.rows_processed.fetch_add(1);
            diag_.last_processed_cursor.store(sample.cursor);
            diag_.last_status = sample.status;

            const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now() - sample.timestamp)
                                     .count();
            diag_.last_latency_ms.store(latency);

            expected_cursor_ = sample.cursor + 1;
            input_queue_.dispose(row);
        }
    }

    SoHEstimateResult SoHTask::process_row(const TelemetryRow &row)
    {
        return estimator_->estimate(row);
    }

} // namespace bms
