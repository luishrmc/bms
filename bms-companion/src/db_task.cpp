#include "db_task.hpp"

#include "snapshot_to_lp.hpp"

namespace bms
{

    DBTask::DBTask(InfluxHTTPClient &client,
                   SnapshotQueue &snapshot_queue,
                   DBTaskConfig cfg)
        : client_(client),
          snapshot_queue_(snapshot_queue),
          cfg_(cfg)
    {
    }

    void DBTask::operator()()
    {
        std::string payload;
        payload.reserve(cfg_.max_payload_bytes);

        std::size_t lines_in_payload = 0;
        BatterySnapshot *snapshot_ptr = nullptr;

        while (true)
        {
            if (snapshot_queue_.try_pop(snapshot_ptr))
            {
            }
            else
            {
                if (snapshot_queue_.is_closed())
                {
                    break;
                }

                const bool got_snapshot =
                    snapshot_queue_.wait_for_and_pop(snapshot_ptr, cfg_.flush_interval);

                if (!got_snapshot)
                {
                    (void)flush_payload_(payload, false);
                    lines_in_payload = 0;
                    continue;
                }
            }

            if (snapshot_ptr != nullptr)
            {
                if (SnapshotToLP::append_battery_snapshot_row(payload, *snapshot_ptr))
                {
                    diagnostics_.snapshot_rows_written += 1;
                    lines_in_payload += 1;
                }

                snapshot_queue_.dispose(snapshot_ptr);
                snapshot_ptr = nullptr;
            }

            while (snapshot_queue_.try_pop(snapshot_ptr))
            {
                if (SnapshotToLP::append_battery_snapshot_row(payload, *snapshot_ptr))
                {
                    diagnostics_.snapshot_rows_written += 1;
                    lines_in_payload += 1;
                }

                snapshot_queue_.dispose(snapshot_ptr);
                snapshot_ptr = nullptr;
            }

            const bool exceed_lines = lines_in_payload >= cfg_.max_lines_per_post;
            const bool exceed_bytes = payload.size() >= cfg_.max_payload_bytes;

            if (exceed_lines || exceed_bytes)
            {
                diagnostics_.threshold_flushes += 1;
                if (!flush_payload_(payload, true))
                {
                    break;
                }
                lines_in_payload = 0;
            }
        }

        while (snapshot_queue_.try_pop(snapshot_ptr))
        {
            if (SnapshotToLP::append_battery_snapshot_row(payload, *snapshot_ptr))
            {
                diagnostics_.snapshot_rows_written += 1;
                lines_in_payload += 1;
            }

            snapshot_queue_.dispose(snapshot_ptr);
            snapshot_ptr = nullptr;
        }

        (void)flush_payload_(payload, false);
    }

    bool DBTask::flush_payload_(std::string &payload, bool threshold_flush)
    {
        if (payload.empty())
        {
            return true;
        }

        std::string error;
        if (!client_.write_lp(payload, error))
        {
            diagnostics_.write_failures += 1;
            diagnostics_.last_error = std::move(error);
            return false;
        }

        diagnostics_.http_posts += 1;
        if (!threshold_flush)
        {
            diagnostics_.timer_flushes += 1;
        }

        payload.clear();
        return true;
    }

} // namespace bms