/**
 * @file        db_consumer.hpp
 * @author      BMS Project
 * @brief       Database Consumer Module for fetching telemetry data
 * @version     0.0.1
 */

#pragma once

#include "batch_pool.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <string>
#include <array>
#include <chrono>

namespace bms
{
    /**
     * Canonical row model for SoC / SoH algorithms
     */
    struct TelemetryRow final
    {
        std::chrono::system_clock::time_point timestamp{};
        std::uint64_t sequence{0}; // Monotonic sequence or cursor for strict ordering

        std::array<float, 16> voltages{};
        std::array<float, 16> currents{};
        std::array<float, 16> temperatures{};

        bool valid{false};
        std::uint32_t status_flags{0};
    };

    using TelemetryRowPool = BatchPool<TelemetryRow>;
    using TelemetryRowQueue = SafeQueue<TelemetryRow, TelemetryRowPool::Deleter>;

    /**
     * Configuration for the database consumer task
     */
    struct DbConsumerConfig final
    {
        boost::chrono::milliseconds polling_interval{1000};
        std::size_t query_limit{100}; // batch size
        std::string ordering_field{"time"};
        int max_retries{3};
        boost::chrono::milliseconds empty_poll_backoff{5000};

        std::string base_url{"http://influxdb3:8181"};
        std::string database{"battery_data"};
        std::string token{""};
    };

    /**
     * DatabaseConsumerTask - Polls InfluxDB (or another DB) for processed telemetry,
     * materializes it into TelemetryRow structures, and dispatches them in strict order
     * to downstream SoC and SoH tasks.
     */
    class DatabaseConsumerTask final
    {
    public:
        DatabaseConsumerTask(
            DbConsumerConfig cfg,
            TelemetryRowPool &row_pool,
            TelemetryRowQueue &soc_queue,
            TelemetryRowQueue &soh_queue);

        DatabaseConsumerTask(const DatabaseConsumerTask &) = delete;
        DatabaseConsumerTask &operator=(const DatabaseConsumerTask &) = delete;

        /**
         * Periodic work function (called by PeriodicTask)
         */
        void operator()();

        // Diagnostics
        std::uint64_t total_rows_fetched() const noexcept { return rows_fetched_.load(); }
        std::uint64_t total_query_failures() const noexcept { return query_failures_.load(); }
        std::uint64_t total_duplicates_skipped() const noexcept { return duplicates_skipped_.load(); }
        std::uint64_t last_processed_sequence() const noexcept { return last_sequence_.load(); }

    private:
        DbConsumerConfig cfg_;
        TelemetryRowPool &pool_;
        TelemetryRowQueue &soc_queue_;
        TelemetryRowQueue &soh_queue_;

        boost::atomic<std::uint64_t> last_sequence_{0}; // Cursor
        boost::chrono::steady_clock::time_point last_empty_poll_{};
        bool in_backoff_{false};

        boost::atomic<std::uint64_t> rows_fetched_{0};
        boost::atomic<std::uint64_t> query_failures_{0};
        boost::atomic<std::uint64_t> duplicates_skipped_{0};

        void fetch_and_dispatch_();
    };

} // namespace bms
