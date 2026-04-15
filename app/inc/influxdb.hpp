/**
 * @file        influxdb.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Header file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#pragma once

#include "db_consumer.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>

#include <algorithm>
#include <charconv>
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

        bool ping() noexcept;
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

        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> total_failures_{0};
        boost::atomic<std::uint64_t> total_retries_{0};
        boost::atomic<int> last_http_code_{0};
    };

    struct ProcessedTelemetryWriterDiagnostics final
    {
        boost::atomic<std::uint64_t> rows_written{0};
        boost::atomic<std::uint64_t> write_failures{0};
        boost::atomic<std::uint64_t> threshold_flushes{0};
        boost::atomic<std::uint64_t> timer_flushes{0};
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
        static constexpr std::size_t kMaxVoltages = 15;
        static constexpr std::size_t kMaxTemperatures = 16;

        void append_row_line_(const TelemetryRow &row);
        bool flush_buffer_();
        bool should_flush_threshold_() const noexcept
        {
            return (buffered_lines_ >= cfg_.max_lines_per_post) || (buffered_bytes_ >= cfg_.max_bytes_per_post);
        }
        bool should_flush_timer_(const boost::chrono::steady_clock::time_point &now) const noexcept
        {
            return !buffer_.empty() && (now - last_flush_time_ >= cfg_.max_buffer_age);
        }

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
        boost::chrono::steady_clock::time_point last_flush_time_{};
        std::string last_error_;

        ProcessedTelemetryWriterDiagnostics diag_{};
        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> post_failures_{0};
    };

} // namespace bms
