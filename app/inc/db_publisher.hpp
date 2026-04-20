/**
 * @file db_publisher.hpp
 * @brief Database publisher task that batches samples and writes InfluxDB line protocol.
 */

#pragma once

#include "temperature.hpp"
#include "voltage_current.hpp"
#include "influxdb.hpp"
#include "safe_queue.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace bms
{
    /**
     * @brief Thresholds controlling payload build-up and flush cadence.
     */
    struct DBPublisherConfig final
    {
        std::size_t max_lines_per_post{256};
        std::size_t max_payload_bytes{128 * 1024};
        std::chrono::milliseconds flush_interval{200};
    };

    /**
     * @brief Runtime counters and last error for publisher diagnostics.
     */
    struct DBPublisherDiagnostics final
    {
        std::uint64_t voltage_rows_written{0};
        std::uint64_t temperature_rows_written{0};
        std::uint64_t http_posts{0};
        std::uint64_t write_failures{0};
        std::uint64_t threshold_flushes{0};
        std::uint64_t timer_flushes{0};
        std::string last_error{};
    };

    /**
     * @brief Consumer task that drains sample queues and posts data to InfluxDB.
     * @note Queue pointers are disposed by this task after serialization.
     */
    class DBPublisherTask final
    {
    public:
        /** @brief Pointer queue type for voltage/current samples. */
        using VoltageQueue = SafeQueue<VoltageCurrentSample>;
        /** @brief Pointer queue type for temperature samples. */
        using TemperatureQueue = SafeQueue<TemperatureSample>;

        /**
         * @brief Creates a publisher bound to two input queues and one HTTP client.
         * @param client InfluxDB HTTP client used for write requests.
         * @param voltage_queue Source queue for voltage/current samples.
         * @param temperature_queue Source queue for temperature samples.
         * @param cfg Flush thresholds and timers.
         */
        DBPublisherTask(InfluxHTTPClient &client,
                        VoltageQueue &voltage_queue,
                        TemperatureQueue &temperature_queue,
                        DBPublisherConfig cfg = DBPublisherConfig{});

        DBPublisherTask(const DBPublisherTask &) = delete;
        DBPublisherTask &operator=(const DBPublisherTask &) = delete;

        /**
         * @brief Runs the publisher loop until both queues are closed and drained.
         */
        void operator()();
        const DBPublisherDiagnostics &diagnostics() const noexcept { return diagnostics_; }

    private:
        bool append_voltage_row_(std::string &payload, const VoltageCurrentSample &sample);
        bool append_temperature_row_(std::string &payload, const TemperatureSample &sample);
        bool flush_payload_(std::string &payload, bool threshold_flush);

        InfluxHTTPClient &client_;
        VoltageQueue &voltage_queue_;
        TemperatureQueue &temperature_queue_;
        DBPublisherConfig cfg_;
        DBPublisherDiagnostics diagnostics_{};
    };

} // namespace bms
