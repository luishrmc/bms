/**
 * @file        db_consumer.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Ordered telemetry database consumption pipeline interfaces.
 * @version     0.0.1
 * @date        2026-04-12
 */

#pragma once

#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bms
{
    /**
     * @brief Canonical row used by SoC/SoH pipeline ingestion.
     *
     * Ordering contract:
     * - @ref cursor must be strictly monotonic and unique for one logical stream.
     * - A row is considered newer when cursor is greater.
     */
    struct TelemetryRow final
    {
        std::uint64_t cursor{0};
        std::chrono::system_clock::time_point timestamp{};
        std::vector<float> voltages{};
        float current_a{0.0F};
        std::vector<float> temperatures{};
        bool valid{true};
        std::string status{};
    };

    /**
     * @brief Result of one database query poll.
     */
    struct TelemetryQueryResult final
    {
        std::vector<TelemetryRow> rows{};
        bool success{true};
        bool transient_failure{false};
        std::string error_message{};
    };

    /**
     * @brief Configuration for periodic database row consumption.
     */
    struct DBConsumerConfig final
    {
        boost::chrono::milliseconds polling_interval{250};
        boost::chrono::milliseconds empty_poll_backoff{500};
        std::size_t query_limit{256};

        std::string ordering_field{"cursor"};

        int max_retries{2};
        boost::chrono::milliseconds retry_delay{100};

        std::uint64_t initial_cursor{0};
    };

    /**
     * @brief Diagnostics counters for ordered query + fan-out stage.
     */
    struct DBConsumerDiagnostics final
    {
        boost::atomic<std::uint64_t> total_rows_fetched{0};
        boost::atomic<std::uint64_t> duplicates_skipped{0};
        boost::atomic<std::uint64_t> out_of_order_rows{0};
        boost::atomic<std::uint64_t> missing_cursor_gaps{0};
        boost::atomic<std::uint64_t> query_failures{0};
        boost::atomic<std::uint64_t> fanout_failures{0};
        boost::atomic<std::uint64_t> last_processed_cursor{0};
        boost::atomic<std::int64_t> last_latency_ms{0};
    };

    /**
     * @brief Abstract query backend used by DB consumer task.
     */
    class ITelemetryQueryBackend
    {
    public:
        virtual ~ITelemetryQueryBackend() = default;

        /**
         * @brief Fetch rows newer than @p cursor ordered by ascending cursor.
         * @param cursor Last successfully consumed cursor.
         * @param limit Maximum rows to return.
         * @return Query result containing ordered rows or error details.
         */
        virtual TelemetryQueryResult fetch_after(
            std::uint64_t cursor,
            std::size_t limit) = 0;
    };

    /**
     * @brief Consumes ordered DB rows and fans them out to SoC/SoH queues.
     */
    class DBConsumerTask final
    {
    public:
        using SharedTelemetryRow = std::shared_ptr<const TelemetryRow>;
        using RowQueue = SafeQueue<SharedTelemetryRow>;

        DBConsumerTask(
            DBConsumerConfig cfg,
            ITelemetryQueryBackend &backend,
            RowQueue &soc_queue,
            RowQueue &soh_queue);

        DBConsumerTask(const DBConsumerTask &) = delete;
        DBConsumerTask &operator=(const DBConsumerTask &) = delete;

        /** @brief Periodic polling entry-point for PeriodicTask. */
        void operator()();

        /** @brief Returns immutable configuration used by this task. */
        const DBConsumerConfig &config() const noexcept { return cfg_; }

        /** @brief Returns task diagnostics. */
        const DBConsumerDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        bool accept_ordered_row_(const TelemetryRow &row);
        bool publish_to_both_(const TelemetryRow &row);
        void record_latency_(const TelemetryRow &row);

        DBConsumerConfig cfg_;
        ITelemetryQueryBackend &backend_;
        RowQueue &soc_queue_;
        RowQueue &soh_queue_;

        std::uint64_t cursor_checkpoint_{0};
        bool applied_empty_backoff_{false};

        DBConsumerDiagnostics diag_{};
    };

} // namespace bms

namespace bms
{
    /**
     * @brief InfluxDB SQL backend configuration for telemetry queries.
     */
    struct InfluxQueryConfig final
    {
        std::string base_url{"http://influxdb3:8181"};
        std::string database{"battery_data"};
        std::string token{};
        std::string table{"processed_telemetry"};

        std::string cursor_column{"cursor"};
        std::string timestamp_column{"time"};
        std::string voltage_column{"voltages"};
        std::string current_column{"current_a"};
        std::string temperature_column{"temperatures"};
        std::string valid_column{"valid"};
        std::string status_column{"status"};

        boost::chrono::milliseconds request_timeout{3000};
        boost::chrono::milliseconds connect_timeout{1000};
    };

    /**
     * @brief InfluxDB-backed query implementation used by DBConsumerTask.
     */
    class InfluxTelemetryQueryBackend final : public ITelemetryQueryBackend
    {
    public:
        explicit InfluxTelemetryQueryBackend(InfluxQueryConfig cfg);
        ~InfluxTelemetryQueryBackend();

        InfluxTelemetryQueryBackend(const InfluxTelemetryQueryBackend &) = delete;
        InfluxTelemetryQueryBackend &operator=(const InfluxTelemetryQueryBackend &) = delete;

        TelemetryQueryResult fetch_after(std::uint64_t cursor, std::size_t limit) override;

    private:
        InfluxQueryConfig cfg_;
        void *curl_{nullptr};
    };

} // namespace bms
