#pragma once

#include "battery_snapshot.hpp"
#include "influxdb.hpp"
#include "safe_queue.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace bms
{

    struct DBTaskConfig
    {
        std::size_t max_lines_per_post{64};
        std::size_t max_payload_bytes{64 * 1024};
        std::chrono::milliseconds flush_interval{500};
    };

    struct DBTaskDiagnostics
    {
        std::size_t snapshot_rows_written{0};
        std::size_t http_posts{0};
        std::size_t write_failures{0};
        std::size_t threshold_flushes{0};
        std::size_t timer_flushes{0};
        std::string last_error;
    };

    class DBTask
    {
    public:
        using SnapshotQueue = SafeQueue<BatterySnapshot>;

        DBTask(InfluxHTTPClient &client,
               SnapshotQueue &snapshot_queue,
               DBTaskConfig cfg);

        void operator()();

        const DBTaskDiagnostics &diagnostics() const noexcept
        {
            return diagnostics_;
        }

    private:
        bool flush_payload_(std::string &payload, bool threshold_flush);

        InfluxHTTPClient &client_;
        SnapshotQueue &snapshot_queue_;
        DBTaskConfig cfg_;
        DBTaskDiagnostics diagnostics_{};
    };

} // namespace bms