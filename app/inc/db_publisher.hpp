#pragma once

#include "batch_structures.hpp"
#include "influxdb.hpp"
#include "safe_queue.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace bms
{
    struct DBPublisherConfig final
    {
        std::size_t max_lines_per_post{256};
        std::size_t max_payload_bytes{128 * 1024};
        std::chrono::milliseconds flush_interval{200};
    };

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

    class DBPublisherTask final
    {
    public:
        using VoltageQueue = SafeQueue<VoltageCurrentSample>;
        using TemperatureQueue = SafeQueue<TemperatureSample>;

        DBPublisherTask(InfluxHTTPClient &client,
                        VoltageQueue &voltage_queue,
                        TemperatureQueue &temperature_queue,
                        DBPublisherConfig cfg = DBPublisherConfig{});

        DBPublisherTask(const DBPublisherTask &) = delete;
        DBPublisherTask &operator=(const DBPublisherTask &) = delete;

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
