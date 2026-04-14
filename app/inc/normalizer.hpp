#pragma once

#include "batch_structures.hpp"
#include "db_consumer.hpp"
#include "influxdb.hpp"

#include <boost/atomic.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace bms
{
    struct NormalizerConfig final
    {
        std::uint64_t initial_cursor{0};
        float default_current_a{0.0F};
    };

    struct NormalizerDiagnostics final
    {
        boost::atomic<std::uint64_t> voltage_batches_consumed{0};
        boost::atomic<std::uint64_t> temperature_batches_consumed{0};
        boost::atomic<std::uint64_t> rows_published{0};
        boost::atomic<std::uint64_t> publish_failures{0};
        boost::atomic<std::uint64_t> rows_without_temperature{0};
        boost::atomic<std::uint64_t> invalid_source_rows{0};
        boost::atomic<std::uint64_t> last_published_cursor{0};
        boost::atomic<std::int64_t> last_latency_ms{0};
    };

    class NormalizerTask final
    {
    public:
        using VoltageQueue = InfluxDBTask::VoltageQueue;
        using TemperatureQueue = InfluxDBTask::TemperatureQueue;
        using RowQueue = DBConsumerTask::RowQueue;

        NormalizerTask(
            NormalizerConfig cfg,
            VoltageQueue &voltage_queue,
            TemperatureQueue &temperature_queue,
            RowQueue &soc_queue,
            RowQueue &soh_queue,
            RowQueue &persistence_queue);

        NormalizerTask(const NormalizerTask &) = delete;
        NormalizerTask &operator=(const NormalizerTask &) = delete;

        void operator()();

        const NormalizerDiagnostics &diagnostics() const noexcept { return diag_; }

    private:
        void consume_temperature_();
        void consume_voltage_and_publish_();
        bool publish_pack_row_();

        static bool has_error_(SampleFlags flags) noexcept
        {
            return any(flags);
        }

        static std::chrono::system_clock::time_point max_ts_(
            const std::chrono::system_clock::time_point &a,
            const std::chrono::system_clock::time_point &b) noexcept
        {
            return (a < b) ? b : a;
        }

        NormalizerConfig cfg_;

        VoltageQueue &voltage_queue_;
        TemperatureQueue &temperature_queue_;
        RowQueue &soc_queue_;
        RowQueue &soh_queue_;
        RowQueue &persistence_queue_;

        std::uint64_t next_cursor_{1};

        std::array<float, 8> device1_voltages_{};
        std::array<float, 8> device2_voltages_{};
        bool have_device1_{false};
        bool have_device2_{false};
        SampleFlags device1_flags_{SampleFlags::None};
        SampleFlags device2_flags_{SampleFlags::None};
        std::chrono::system_clock::time_point device1_ts_{};
        std::chrono::system_clock::time_point device2_ts_{};

        std::array<float, 16> latest_temperatures_{};
        bool have_temperature_{false};
        SampleFlags temperature_flags_{SampleFlags::None};
        std::chrono::system_clock::time_point temperature_ts_{};

        NormalizerDiagnostics diag_{};
    };

} // namespace bms
