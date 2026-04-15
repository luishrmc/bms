#include "normalizer.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace bms
{
    float PlaceholderVoltageCurrentSource::estimate_pack_current_a(const std::array<float, 8> &device1_voltages) const noexcept
    {
        // TODO(bms-current-sensor): Replace this placeholder with the real current sensor path.
        // Temporary behavior intentionally derives input from Device 1 voltage acquisition only.
        const float proxy_voltage_v = device1_voltages[0]; // Device 1 ch0 (cell1 voltage)
        if (!std::isfinite(proxy_voltage_v))
        {
            return 0.0F;
        }

        // Placeholder output: deterministic stub while preserving integration seam.
        return proxy_voltage_v * 0.0F;
    }

    NormalizerTask::NormalizerTask(
        NormalizerConfig cfg,
        const ICurrentSource &current_source,
        VoltageQueue &voltage_queue,
        TemperatureQueue &temperature_queue,
        RowQueue &soc_queue,
        RowQueue &soh_queue,
        RowQueue &persistence_queue)
        : cfg_(std::move(cfg)),
          current_source_(current_source),
          voltage_queue_(voltage_queue),
          temperature_queue_(temperature_queue),
          soc_queue_(soc_queue),
          soh_queue_(soh_queue),
          persistence_queue_(persistence_queue),
          next_cursor_(cfg_.initial_cursor + 1)
    {
        diag_.last_published_cursor.store(cfg_.initial_cursor);
    }

    void NormalizerTask::operator()()
    {
        consume_temperature_();
        consume_voltage_and_publish_();
    }

    void NormalizerTask::consume_temperature_()
    {
        TemperatureBatch *batch = nullptr;
        while (temperature_queue_.try_pop(batch))
        {
            if (batch == nullptr)
            {
                continue;
            }

            latest_temperatures_ = batch->temperatures;
            have_temperature_ = true;
            temperature_flags_ = batch->flags;
            temperature_ts_ = batch->ts.timestamp;

            diag_.temperature_batches_consumed.fetch_add(1);
            temperature_queue_.dispose(batch);
        }
    }

    void NormalizerTask::consume_voltage_and_publish_()
    {
        VoltageBatch *batch = nullptr;
        while (voltage_queue_.try_pop(batch))
        {
            if (batch == nullptr)
            {
                continue;
            }

            if (batch->device_id == 1)
            {
                device1_voltages_ = batch->voltages;
                have_device1_ = true;
                device1_flags_ = batch->flags;
                device1_ts_ = batch->ts.timestamp;
            }
            else if (batch->device_id == 2)
            {
                device2_voltages_ = batch->voltages;
                have_device2_ = true;
                device2_flags_ = batch->flags;
                device2_ts_ = batch->ts.timestamp;
            }
            else
            {
                diag_.invalid_source_rows.fetch_add(1);
            }

            diag_.voltage_batches_consumed.fetch_add(1);
            voltage_queue_.dispose(batch);

            // Publish one pack-level row per completed device1+device2 pair.
            if (have_device1_ && have_device2_)
            {
                if (!publish_pack_row_())
                {
                    diag_.publish_failures.fetch_add(1);
                }

                // Always clear pair latches after a publish attempt so the next row
                // requires fresh data from both halves and cannot mix stale values.
                clear_voltage_pair_latches_();
            }
        }
    }

    void NormalizerTask::clear_voltage_pair_latches_() noexcept
    {
        have_device1_ = false;
        have_device2_ = false;
        device1_flags_ = SampleFlags::None;
        device2_flags_ = SampleFlags::None;
    }

    bool NormalizerTask::publish_pack_row_()
    {
        TelemetryRow row;
        row.cursor = next_cursor_++;

        // Timestamp policy: choose latest timestamp among contributing source batches.
        row.timestamp = max_ts_(device1_ts_, device2_ts_);
        if (have_temperature_)
        {
            row.timestamp = max_ts_(row.timestamp, temperature_ts_);
        }

        row.voltages.reserve(kPackCellCount);

        // Explicit battery-cell mapping (15-cell pack):
        // Device 1 @192.168.7.2:
        //   ch0->cell1, ch1->cell2, ch2->cell3, ch3->cell4,
        //   ch4->cell5, ch5->cell6, ch6->cell7, ch7->cell8.
        row.voltages.insert(row.voltages.end(), device1_voltages_.begin(), device1_voltages_.end());

        // Device 2:
        //   ch0->cell9, ch1->cell10, ch2->cell11, ch3->cell12,
        //   ch4->cell13, ch5->cell14, ch6->cell15.
        //   ch7 is NOT a battery cell voltage and is intentionally excluded.
        row.voltages.insert(row.voltages.end(), device2_voltages_.begin(), device2_voltages_.begin() + 7);

        row.current_a = current_source_.estimate_pack_current_a(device1_voltages_);

        if (have_temperature_)
        {
            row.temperatures.assign(latest_temperatures_.begin(), latest_temperatures_.end());
        }

        const bool voltage_ok = !has_error_(device1_flags_) && !has_error_(device2_flags_);
        const bool temp_ok = have_temperature_ && !has_error_(temperature_flags_);
        row.valid = voltage_ok && temp_ok;

        if (!have_temperature_)
        {
            row.status = "missing_temperature_snapshot";
            diag_.rows_without_temperature.fetch_add(1);
        }
        else if (!row.valid)
        {
            row.status = "source_flagged";
            diag_.invalid_source_rows.fetch_add(1);
        }
        else
        {
            row.status = "ok";
        }

        auto *shared_row = new TelemetryRow(std::move(row));

        shared_row->add_ref();
        if (!soc_queue_.push(shared_row))
        {
            shared_row->release();
            return false;
        }

        shared_row->add_ref();
        if (!soh_queue_.push(shared_row))
        {
            shared_row->release();
            TelemetryRow *rollback = nullptr;
            if (soc_queue_.try_pop(rollback))
            {
                if (rollback == shared_row)
                {
                    soc_queue_.dispose(rollback);
                }
                else if (rollback != nullptr)
                {
                    if (!soc_queue_.push(rollback))
                    {
                        soc_queue_.dispose(rollback);
                    }
                }
            }
            return false;
        }

        shared_row->add_ref();
        if (!persistence_queue_.push(shared_row))
        {
            shared_row->release();

            TelemetryRow *rollback = nullptr;
            if (soh_queue_.try_pop(rollback))
            {
                if (rollback == shared_row)
                {
                    soh_queue_.dispose(rollback);
                }
                else if (rollback != nullptr)
                {
                    if (!soh_queue_.push(rollback))
                    {
                        soh_queue_.dispose(rollback);
                    }
                }
            }

            rollback = nullptr;
            if (soc_queue_.try_pop(rollback))
            {
                if (rollback == shared_row)
                {
                    soc_queue_.dispose(rollback);
                }
                else if (rollback != nullptr)
                {
                    if (!soc_queue_.push(rollback))
                    {
                        soc_queue_.dispose(rollback);
                    }
                }
            }
            return false;
        }

        diag_.rows_published.fetch_add(1);
        diag_.last_published_cursor.store(shared_row->cursor);

        const auto now = std::chrono::system_clock::now();
        const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - shared_row->timestamp).count();
        diag_.last_latency_ms.store(latency_ms);
        return true;
    }

} // namespace bms
