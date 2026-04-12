/**
 * @file        soc.hpp
 * @author      BMS Project
 * @brief       State of Charge (SoC) Processing Module
 * @version     0.0.1
 */

#pragma once

#include "db_consumer.hpp"
#include <boost/atomic.hpp>

namespace bms
{
    /**
     * SoCTask - Dedicated task for SoC processing.
     * Consumes TelemetryRows strictly in order, processes them,
     * and maintains its own diagnostics.
     */
    class SoCTask final
    {
    public:
        SoCTask(TelemetryRowQueue &queue, TelemetryRowPool &pool);

        SoCTask(const SoCTask &) = delete;
        SoCTask &operator=(const SoCTask &) = delete;

        /**
         * Periodic work function
         */
        void operator()();

        // Diagnostics
        std::uint64_t total_rows_processed() const noexcept { return rows_processed_.load(); }
        std::uint64_t total_out_of_order() const noexcept { return out_of_order_.load(); }
        std::uint64_t total_processing_failures() const noexcept { return processing_failures_.load(); }
        std::uint64_t last_processed_sequence() const noexcept { return last_sequence_.load(); }
        std::int64_t last_latency_ms() const noexcept { return last_latency_ms_.load(); }

    private:
        TelemetryRowQueue &queue_;
        TelemetryRowPool &pool_;

        boost::atomic<std::uint64_t> rows_processed_{0};
        boost::atomic<std::uint64_t> out_of_order_{0};
        boost::atomic<std::uint64_t> processing_failures_{0};
        boost::atomic<std::uint64_t> last_sequence_{0};
        boost::atomic<std::int64_t> last_latency_ms_{0};

        void process_row_(const TelemetryRow &row);
    };

} // namespace bms
