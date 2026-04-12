/**
 * @file        soh.cpp
 * @author      BMS Project
 * @brief       Implementation of SoH Processing Module
 * @version     0.0.1
 */

#include "soh.hpp"
#include <iostream>

namespace bms
{

    SoHTask::SoHTask(TelemetryRowQueue &queue, TelemetryRowPool &pool)
        : queue_(queue), pool_(pool)
    {
    }

    void SoHTask::operator()()
    {
        TelemetryRow *row = nullptr;

        while (queue_.try_pop(row))
        {
            if (!row)
                continue;

            // Strict ordering check
            if (row->sequence > 0 && row->sequence <= last_sequence_)
            {
                out_of_order_.fetch_add(1, boost::memory_order_relaxed);
            }
            else
            {
                try
                {
                    process_row_(*row);
                    last_sequence_.store(row->sequence, boost::memory_order_relaxed);
                    rows_processed_.fetch_add(1, boost::memory_order_relaxed);

                    auto now = std::chrono::system_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - row->timestamp).count();
                    last_latency_ms_.store(latency, boost::memory_order_relaxed);
                }
                catch (...)
                {
                    processing_failures_.fetch_add(1, boost::memory_order_relaxed);
                }
            }

            // Return to pool
            pool_.release(row);
        }
    }

    void SoHTask::process_row_(const TelemetryRow &row)
    {
        // TODO: Insert State of Health (SoH) algorithm logic here.
        // Currently a placeholder for future algorithm integration.
        // The algorithm should compute SoH based on long-term data trends from `row`.

        (void)row; // Suppress unused warning
    }

} // namespace bms
