/**
 * @file        db_consumer.cpp
 * @author      BMS Project
 * @brief       Implementation of Database Consumer Module
 * @version     0.0.1
 */

#include "db_consumer.hpp"
#include <iostream>

namespace bms
{

    DatabaseConsumerTask::DatabaseConsumerTask(
        DbConsumerConfig cfg,
        TelemetryRowPool &row_pool,
        TelemetryRowQueue &soc_queue,
        TelemetryRowQueue &soh_queue)
        : cfg_(std::move(cfg)),
          pool_(row_pool),
          soc_queue_(soc_queue),
          soh_queue_(soh_queue)
    {
    }

    void DatabaseConsumerTask::operator()()
    {
        // Handle empty-poll backoff policy
        if (in_backoff_)
        {
            auto now = boost::chrono::steady_clock::now();
            if (now - last_empty_poll_ < cfg_.empty_poll_backoff)
            {
                return; // Still in backoff
            }
            in_backoff_ = false;
        }

        try
        {
            fetch_and_dispatch_();
        }
        catch (const std::exception &e)
        {
            query_failures_.fetch_add(1, boost::memory_order_relaxed);
            // Log error mechanism would go here
        }
    }

    void DatabaseConsumerTask::fetch_and_dispatch_()
    {
        // TODO: Implement actual database query logic here.
        // The logic should formulate a query using `last_sequence_` as a cursor
        // (or timestamp), and retrieve up to `cfg_.query_limit` rows ordered by `cfg_.ordering_field`.

        // Simulated fetching logic
        bool no_new_data = true; // For now, we simulate an empty DB response to avoid infinite fake data
        std::size_t fetched_in_this_poll = 0;

        // Example iteration logic when real DB result is available:
        // for (const auto& db_row : query_results) {
        //     if (db_row.sequence <= last_sequence_) {
        //         duplicates_skipped_.fetch_add(1, boost::memory_order_relaxed);
        //         continue; // Enforce strict monotonic sequence
        //     }
        //
        //     // We duplicate the row so both SoC and SoH can process independently
        //     TelemetryRow* soc_row = pool_.acquire();
        //     TelemetryRow* soh_row = pool_.acquire();
        //
        //     if (soc_row && soh_row) {
        //         // Populate rows
        //         *soc_row = db_row;
        //         *soh_row = db_row;
        //
        //         // Push to downstream queues
        //         bool pushed_soc = soc_queue_.push(soc_row);
        //         bool pushed_soh = soh_queue_.push(soh_row);
        //
        //         if (!pushed_soc) pool_.release(soc_row);
        //         if (!pushed_soh) pool_.release(soh_row);
        //
        //         if (pushed_soc || pushed_soh) {
        //             last_sequence_.store(db_row.sequence, boost::memory_order_relaxed);
        //             rows_fetched_.fetch_add(1, boost::memory_order_relaxed);
        //             fetched_in_this_poll++;
        //         }
        //     } else {
        //         // Handle pool exhaustion
        //         if (soc_row) pool_.release(soc_row);
        //         if (soh_row) pool_.release(soh_row);
        //         break;
        //     }
        // }

        if (no_new_data && fetched_in_this_poll == 0)
        {
            in_backoff_ = true;
            last_empty_poll_ = boost::chrono::steady_clock::now();
        }
    }

} // namespace bms
