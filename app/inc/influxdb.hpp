/**
 * @file        influxdb.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Header file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#pragma once

#include "batch_pool.hpp"
#include "batch_structures.hpp"
#include "db_consumer.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <charconv>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bms
{
    /**
     * InfluxDBConfig - InfluxDB 3.x HTTP API configuration
     */
    struct InfluxDBConfig final
    {
        // Server connection
        std::string base_url{"http://influxdb3:8181"};
        std::string database{"battery_data"};
        std::string token{""};

        // Table names (measurements in Line Protocol)
        std::string voltage1_table{"voltage1"};
        std::string voltage2_table{"voltage2"};
        std::string temperature_table{"temperature"};

        // HTTP timeouts
        boost::chrono::milliseconds connect_timeout{1500};
        boost::chrono::milliseconds request_timeout{5000};

        // Batching (optimize HTTP requests)
        std::size_t max_lines_per_post{2048};
        std::size_t max_bytes_per_post{512 * 1024};
        boost::chrono::milliseconds max_buffer_age{250};

        // Retry policy
        int max_retries{3};
        boost::chrono::milliseconds retry_delay{100};

        // Data policy
        bool include_invalid_samples{false}; // Write flagged samples to DB

        // Precision control
        int voltage_precision{6};     // Voltage: 3.289765 V
        int temperature_precision{3}; // Temperature: 25.123 °C
    };

    /**
     * InfluxHTTPClient - Minimal HTTP client for InfluxDB Line Protocol
     *
     * Thread Safety: NOT thread-safe. Use one instance per consumer thread.
     */
    class InfluxHTTPClient final
    {
    public:
        explicit InfluxHTTPClient(const InfluxDBConfig &cfg);
        ~InfluxHTTPClient();

        InfluxHTTPClient(const InfluxHTTPClient &) = delete;
        InfluxHTTPClient &operator=(const InfluxHTTPClient &) = delete;

        /**
         * Test connectivity to InfluxDB server
         * @return true if /ping returns 204
         */
        bool ping() noexcept;

        /**
         * Write Line Protocol payload to InfluxDB
         * @param payload Line Protocol string (newline-separated)
         * @param error_out Error message output
         * @return true if HTTP 204 received
         */
        bool write_lp(const std::string &payload, std::string &error_out) noexcept;

        // Diagnostics (thread-safe)
        std::uint64_t total_posts() const noexcept { return total_posts_.load(); }
        std::uint64_t total_failures() const noexcept { return total_failures_.load(); }
        std::uint64_t total_retries() const noexcept { return total_retries_.load(); }
        int last_http_code() const noexcept { return last_http_code_.load(); }

        const InfluxDBConfig &config() const noexcept { return cfg_; }

    private:
        std::string make_write_url_() const;
        std::string make_ping_url_() const;

        InfluxDBConfig cfg_;
        void *curl_{nullptr};    // CURL*
        void *headers_{nullptr}; // curl_slist*

        // Atomic counters for thread-safe diagnostics
        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> total_failures_{0};
        boost::atomic<std::uint64_t> total_retries_{0};
        boost::atomic<int> last_http_code_{0};
    };

    /**
     * InfluxDBTask - Unified consumer for voltage and temperature queues
     *
     * Periodically drains queues, batches Line Protocol, and POSTs to InfluxDB.
     *
     * Voltage Tables:
     *   - voltage1 (device_id=1): time, ch0..ch7
     *   - voltage2 (device_id=2): time, ch0..ch7
     *
     * Temperature Table:
     *   - temperature: time, sensor0..sensor15
     *
     * Usage with PeriodicTask:
     *   InfluxHTTPClient client(config);
     *   InfluxDBTask task(config, client, vpool, tpool, vqueue, tqueue);
     *   PeriodicTask writer(boost::chrono::milliseconds(100), std::ref(task));
     *   writer.start();
     */
    class InfluxDBTask final
    {
    public:
        using VoltageQueue = SafeQueue<VoltageBatch, VoltageBatchPool::Deleter>;
        using TemperatureQueue = SafeQueue<TemperatureBatch, TemperatureBatchPool::Deleter>;

        InfluxDBTask(
            InfluxDBConfig cfg,
            InfluxHTTPClient &client,
            VoltageBatchPool &vpool,
            TemperatureBatchPool &tpool,
            VoltageQueue &vq,
            TemperatureQueue &tq);

        InfluxDBTask(const InfluxDBTask &) = delete;
        InfluxDBTask &operator=(const InfluxDBTask &) = delete;

        /**
         * Periodic work function (called by PeriodicTask)
         * Drains queues and writes batches to InfluxDB
         */
        void operator()();

        // Diagnostics (thread-safe)
        std::uint64_t total_posts() const noexcept { return total_posts_.load(); }
        std::uint64_t total_post_failures() const noexcept { return post_failures_.load(); }
        std::uint64_t total_voltage_samples() const noexcept { return voltage_samples_.load(); }
        std::uint64_t total_temperature_samples() const noexcept { return temperature_samples_.load(); }
        std::uint64_t dropped_flagged_samples() const noexcept { return dropped_flagged_.load(); }
        std::uint64_t threshold_flushes() const noexcept { return threshold_flushes_.load(); }
        std::uint64_t timer_flushes() const noexcept { return timer_flushes_.load(); }

        const std::string &last_error() const noexcept { return last_error_; }

    private:
        static constexpr std::size_t kVoltageChannelsPerDevice = 8;
        static constexpr std::size_t kTempSensors = 16;

        void drain_voltage_();
        void drain_temperature_();

        void append_voltage_line_(const VoltageBatch &b);
        void append_temperature_line_(const TemperatureBatch &b);

        bool should_flush_threshold_() const noexcept
        {
            return (buffered_lines_ >= cfg_.max_lines_per_post) || (buffered_bytes_ >= cfg_.max_bytes_per_post);
        }

        bool should_flush_timer_(const boost::chrono::steady_clock::time_point &now) const noexcept
        {
            return !buffer_.empty() && (now - last_flush_time_ >= cfg_.max_buffer_age);
        }

        void flush_buffer_(bool threshold_triggered);

        static std::int64_t to_influxdb_ns_(const std::chrono::system_clock::time_point &tp) noexcept
        {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(tp.time_since_epoch()).count();
        }

        static std::string escape_measurement_(const std::string &m)
        {
            std::string out;
            out.reserve(m.size());
            for (char c : m)
            {
                if (c == ' ' || c == ',' || c == '=')
                    out.push_back('\\');
                out.push_back(c);
            }
            return out;
        }

        static void append_unsigned_(std::string &out, std::size_t value)
        {
            char buf[32];
            const auto result = std::to_chars(buf, buf + sizeof(buf), value);
            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }
            out += std::to_string(value);
        }

        static void append_int64_(std::string &out, std::int64_t value)
        {
            char buf[32];
            const auto result = std::to_chars(buf, buf + sizeof(buf), value);
            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }
            out += std::to_string(value);
        }

        static void append_float_fixed_(std::string &out, double value, int precision)
        {
            char buf[64];
            const auto result = std::to_chars(
                buf,
                buf + sizeof(buf),
                value,
                std::chars_format::fixed,
                precision);

            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }

            // Toolchain fallback for floating-point to_chars limitations.
            const int count = std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
            if (count > 0)
            {
                const std::size_t len = static_cast<std::size_t>(count);
                out.append(buf, (len < sizeof(buf)) ? len : sizeof(buf) - 1);
                return;
            }

            out += "0.0";
        }

        InfluxDBConfig cfg_;
        InfluxHTTPClient &client_;

        VoltageBatchPool &vpool_;
        TemperatureBatchPool &tpool_;
        VoltageQueue &vq_;
        TemperatureQueue &tq_;

        std::string buffer_;
        std::size_t buffered_lines_{0};
        std::size_t buffered_bytes_{0};
        boost::chrono::steady_clock::time_point last_flush_time_{};
        std::string last_error_;

        // Atomic counters for thread-safe diagnostics
        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> post_failures_{0};
        boost::atomic<std::uint64_t> voltage_samples_{0};
        boost::atomic<std::uint64_t> temperature_samples_{0};
        boost::atomic<std::uint64_t> dropped_flagged_{0};
        boost::atomic<std::uint64_t> threshold_flushes_{0};
        boost::atomic<std::uint64_t> timer_flushes_{0};
    };

    // ===== Inline implementation (header-only consumer logic) ====================

    inline InfluxDBTask::InfluxDBTask(
        InfluxDBConfig cfg,
        InfluxHTTPClient &client,
        VoltageBatchPool &vpool,
        TemperatureBatchPool &tpool,
        VoltageQueue &vq,
        TemperatureQueue &tq)
        : cfg_(std::move(cfg)), client_(client), vpool_(vpool), tpool_(tpool), vq_(vq), tq_(tq)
    {
        buffer_.reserve(64 * 1024);
        last_flush_time_ = boost::chrono::steady_clock::now();
    }

    inline void InfluxDBTask::operator()()
    {
        last_error_.clear();

        drain_voltage_();
        drain_temperature_();

        const boost::chrono::steady_clock::time_point now = boost::chrono::steady_clock::now();
        if (should_flush_timer_(now))
        {
            flush_buffer_(false);
        }
    }

    inline void InfluxDBTask::drain_voltage_()
    {
        VoltageBatch *batch = nullptr;

        while (vq_.try_pop(batch))
        {
            // Validate batch using centralized function
            const SampleFlags flags = validate_voltage_batch(*batch);

            if (any(flags) && !cfg_.include_invalid_samples)
            {
                dropped_flagged_.fetch_add(1);
                vpool_.release(batch);
                continue;
            }

            append_voltage_line_(*batch);
            vpool_.release(batch);

            voltage_samples_.fetch_add(1);

            if (should_flush_threshold_())
            {
                flush_buffer_(true);
            }
        }
    }

    inline void InfluxDBTask::drain_temperature_()
    {
        TemperatureBatch *batch = nullptr;

        while (tq_.try_pop(batch))
        {
            // Validate batch using centralized function
            const SampleFlags flags = validate_temperature_batch(*batch);

            if (any(flags) && !cfg_.include_invalid_samples)
            {
                dropped_flagged_.fetch_add(1);
                tpool_.release(batch);
                continue;
            }

            append_temperature_line_(*batch);
            tpool_.release(batch);

            temperature_samples_.fetch_add(1);

            if (should_flush_threshold_())
            {
                flush_buffer_(true);
            }
        }
    }

    inline void InfluxDBTask::append_voltage_line_(const VoltageBatch &b)
    {
        const std::int64_t ts_ns = to_influxdb_ns_(b.ts.timestamp);

        // Select table based on device_id
        const std::string &table =
            (b.device_id == 1) ? cfg_.voltage1_table : (b.device_id == 2) ? cfg_.voltage2_table
                                                                          : cfg_.voltage1_table; // Fallback to voltage1 for unknown devices

        buffer_ += escape_measurement_(table);
        buffer_ += " ";

        // Fields: ch0..ch7
        const std::size_t n = (b.voltages.size() < kVoltageChannelsPerDevice)
                                  ? b.voltages.size()
                                  : kVoltageChannelsPerDevice;

        for (std::size_t i = 0; i < n; ++i)
        {
            if (i > 0)
                buffer_ += ",";

            buffer_ += "ch";
            append_unsigned_(buffer_, i);
            buffer_ += "=";
            append_float_fixed_(buffer_, b.voltages[i], cfg_.voltage_precision);
        }

        buffer_ += " ";
        append_int64_(buffer_, ts_ns);
        buffer_ += "\n";

        ++buffered_lines_;
        buffered_bytes_ = buffer_.size();
    }

    inline void InfluxDBTask::append_temperature_line_(const TemperatureBatch &b)
    {
        const std::int64_t ts_ns = to_influxdb_ns_(b.ts.timestamp);

        buffer_ += escape_measurement_(cfg_.temperature_table);
        buffer_ += " ";

        // Fields: sensor0..sensor15
        for (std::size_t i = 0; i < kTempSensors && i < b.temperatures.size(); ++i)
        {
            if (i > 0)
                buffer_ += ",";

            buffer_ += "sensor";
            append_unsigned_(buffer_, i);
            buffer_ += "=";
            append_float_fixed_(buffer_, b.temperatures[i], cfg_.temperature_precision);
        }

        buffer_ += " ";
        append_int64_(buffer_, ts_ns);
        buffer_ += "\n";

        ++buffered_lines_;
        buffered_bytes_ = buffer_.size();
    }

    inline void InfluxDBTask::flush_buffer_(bool threshold_triggered)
    {
        if (buffer_.empty())
        {
            return;
        }

        if (threshold_triggered)
        {
            threshold_flushes_.fetch_add(1);
        }
        else
        {
            timer_flushes_.fetch_add(1);
        }

        std::string err;
        if (!client_.write_lp(buffer_, err))
        {
            post_failures_.fetch_add(1);
            last_error_ = std::move(err);
            // Explicit bounded-memory policy: drop failed payload.
        }
        else
        {
            total_posts_.fetch_add(1);
        }

        buffer_.clear();
        buffered_lines_ = 0;
        buffered_bytes_ = 0;
        last_flush_time_ = boost::chrono::steady_clock::now();
    }

    struct ProcessedTelemetryWriterDiagnostics final
    {
        boost::atomic<std::uint64_t> rows_written{0};
        boost::atomic<std::uint64_t> write_failures{0};
    };

    class ProcessedTelemetryWriterTask final
    {
    public:
        using RowQueue = SafeQueue<TelemetryRow, SharedTelemetryDisposer>;

        ProcessedTelemetryWriterTask(
            InfluxDBConfig cfg,
            InfluxHTTPClient &client,
            RowQueue &queue);

        ProcessedTelemetryWriterTask(const ProcessedTelemetryWriterTask &) = delete;
        ProcessedTelemetryWriterTask &operator=(const ProcessedTelemetryWriterTask &) = delete;

        void operator()();

        std::uint64_t total_posts() const noexcept { return total_posts_.load(); }
        std::uint64_t total_post_failures() const noexcept { return post_failures_.load(); }
        const ProcessedTelemetryWriterDiagnostics &diagnostics() const noexcept { return diag_; }
        const std::string &last_error() const noexcept { return last_error_; }

    private:
        static constexpr std::size_t kMaxVoltages = 16;
        static constexpr std::size_t kMaxTemperatures = 16;

        void append_row_line_(const TelemetryRow &row);
        bool flush_buffer_();

        static std::int64_t to_influxdb_ns_(const std::chrono::system_clock::time_point &tp) noexcept
        {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(tp.time_since_epoch()).count();
        }

        static void append_int64_(std::string &out, std::int64_t value)
        {
            char buf[32];
            const auto result = std::to_chars(buf, buf + sizeof(buf), value);
            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }
            out += std::to_string(value);
        }

        static void append_uint64_(std::string &out, std::uint64_t value)
        {
            char buf[32];
            const auto result = std::to_chars(buf, buf + sizeof(buf), value);
            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }
            out += std::to_string(value);
        }

        static void append_float_fixed_(std::string &out, double value, int precision)
        {
            char buf[64];
            const auto result = std::to_chars(
                buf,
                buf + sizeof(buf),
                value,
                std::chars_format::fixed,
                precision);
            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }

            const int count = std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
            if (count > 0)
            {
                const std::size_t len = static_cast<std::size_t>(count);
                out.append(buf, (len < sizeof(buf)) ? len : sizeof(buf) - 1);
                return;
            }

            out += "0.0";
        }

        static void append_escaped_string_(std::string &out, const std::string &value)
        {
            out.push_back('"');
            for (char c : value)
            {
                if (c == '"' || c == '\\')
                {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            out.push_back('"');
        }

        static void append_numeric_array_json_(std::string &out, const std::vector<float> &values, std::size_t max_values, int precision)
        {
            out.push_back('[');
            const std::size_t count = std::min(values.size(), max_values);
            for (std::size_t i = 0; i < count; ++i)
            {
                if (i > 0)
                {
                    out.push_back(',');
                }
                append_float_fixed_(out, values[i], precision);
            }
            out.push_back(']');
        }

        InfluxDBConfig cfg_;
        InfluxHTTPClient &client_;
        RowQueue &queue_;
        std::string table_name_{"processed_telemetry"};
        std::string buffer_;
        std::size_t buffered_lines_{0};
        std::size_t buffered_bytes_{0};
        std::string last_error_;

        ProcessedTelemetryWriterDiagnostics diag_{};
        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> post_failures_{0};
    };

} // namespace bms
