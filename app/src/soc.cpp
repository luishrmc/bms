/**
 * @file        soc.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       SoC task scaffold implementation.
 * @version     0.0.1
 * @date        2026-04-12
 */

#include "soc.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace bms
{
    SoCTask::SoCTask(SoCTaskConfig cfg, RowQueue &input_queue)
        : cfg_(std::move(cfg)), input_queue_(input_queue), expected_cursor_(cfg_.initial_expected_cursor)
    {
    }

    void SoCTask::operator()()
    {
        SharedTelemetryRow *row = nullptr;
        while (input_queue_.try_pop(row))
        {
            if (!row || !(*row))
            {
                delete row;
                continue;
            }

            const TelemetryRow &sample = *(*row);

            if (sample.cursor < expected_cursor_)
            {
                diag_.duplicates_skipped.fetch_add(1);
                delete row;
                continue;
            }

            if (sample.cursor > expected_cursor_)
            {
                diag_.out_of_order_rows.fetch_add(1);
                std::cerr << "[SoC] Out-of-order cursor: expected " << expected_cursor_
                          << " got " << sample.cursor << std::endl;
                expected_cursor_ = sample.cursor;
            }

            if (!process_row(sample))
            {
                diag_.processing_failures.fetch_add(1);
                delete row;
                continue;
            }

            diag_.rows_processed.fetch_add(1);
            diag_.last_processed_cursor.store(sample.cursor);
            diag_.last_status = sample.status;

            const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now() - sample.timestamp)
                                     .count();
            diag_.last_latency_ms.store(latency);

            expected_cursor_ = sample.cursor + 1;
            delete row;
        }
    }

    bool SoCTask::process_row(const TelemetryRow &row)
    {
        (void)row;
        // TODO(bms-soc): plug real SoC estimator here using ordered TelemetryRow input.
        return true;
    }

} // namespace bms
