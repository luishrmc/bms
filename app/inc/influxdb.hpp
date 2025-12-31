#pragma once

#include "batch_pool.hpp"
#include "batch_structures.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <cstdint>
#include <sstream>
#include <string>

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

        const std::string &last_error() const noexcept { return last_error_; }

    private:
        static constexpr std::size_t kVoltageChannelsPerDevice = 8;
        static constexpr std::size_t kTempSensors = 16;

        void drain_voltage_(std::size_t &lines, std::size_t &bytes);
        void drain_temperature_(std::size_t &lines, std::size_t &bytes);

        void append_voltage_line_(const VoltageBatch &b, std::size_t &lines, std::size_t &bytes);
        void append_temperature_line_(const TemperatureBatch &b, std::size_t &lines, std::size_t &bytes);

        bool should_flush_(std::size_t lines, std::size_t bytes) const noexcept
        {
            return (lines >= cfg_.max_lines_per_post) || (bytes >= cfg_.max_bytes_per_post);
        }

        void flush_buffer_(std::size_t lines);

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

        InfluxDBConfig cfg_;
        InfluxHTTPClient &client_;

        VoltageBatchPool &vpool_;
        TemperatureBatchPool &tpool_;
        VoltageQueue &vq_;
        TemperatureQueue &tq_;

        std::string buffer_;
        std::string last_error_;

        // Atomic counters for thread-safe diagnostics
        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> post_failures_{0};
        boost::atomic<std::uint64_t> voltage_samples_{0};
        boost::atomic<std::uint64_t> temperature_samples_{0};
        boost::atomic<std::uint64_t> dropped_flagged_{0};
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
    }

    inline void InfluxDBTask::operator()()
    {
        std::size_t lines = 0;
        std::size_t bytes = 0;

        buffer_.clear();
        last_error_.clear();

        drain_voltage_(lines, bytes);
        drain_temperature_(lines, bytes);

        if (!buffer_.empty())
        {
            flush_buffer_(lines);
        }
    }

    inline void InfluxDBTask::drain_voltage_(std::size_t &lines, std::size_t &bytes)
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

            append_voltage_line_(*batch, lines, bytes);
            vpool_.release(batch);

            voltage_samples_.fetch_add(1);

            if (should_flush_(lines, bytes))
            {
                flush_buffer_(lines);
                buffer_.clear();
                lines = 0;
                bytes = 0;
            }
        }
    }

    inline void InfluxDBTask::drain_temperature_(std::size_t &lines, std::size_t &bytes)
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

            append_temperature_line_(*batch, lines, bytes);
            tpool_.release(batch);

            temperature_samples_.fetch_add(1);

            if (should_flush_(lines, bytes))
            {
                flush_buffer_(lines);
                buffer_.clear();
                lines = 0;
                bytes = 0;
            }
        }
    }

    inline void InfluxDBTask::append_voltage_line_(const VoltageBatch &b, std::size_t &lines, std::size_t &bytes)
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
            buffer_ += std::to_string(i);
            buffer_ += "=";

            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(cfg_.voltage_precision);
            oss << b.voltages[i];
            buffer_ += oss.str();
        }

        buffer_ += " ";
        buffer_ += std::to_string(ts_ns);
        buffer_ += "\n";

        lines++;
        bytes = buffer_.size();
    }

    inline void InfluxDBTask::append_temperature_line_(const TemperatureBatch &b, std::size_t &lines, std::size_t &bytes)
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
            buffer_ += std::to_string(i);
            buffer_ += "=";

            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(cfg_.temperature_precision);
            oss << b.temperatures[i];
            buffer_ += oss.str();
        }

        buffer_ += " ";
        buffer_ += std::to_string(ts_ns);
        buffer_ += "\n";

        lines++;
        bytes = buffer_.size();
    }

    inline void InfluxDBTask::flush_buffer_(std::size_t lines)
    {
        std::string err;
        if (!client_.write_lp(buffer_, err))
        {
            post_failures_.fetch_add(1);
            last_error_ = std::move(err);
            // Drop payload on failure (bounded memory)
            return;
        }

        total_posts_.fetch_add(1);
        (void)lines;
    }

} // namespace bms
