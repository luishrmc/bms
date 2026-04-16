#include "db_publisher.hpp"

#include <charconv>
#include <string>

namespace bms
{
    namespace
    {
        void append_uint64(std::string &out, std::uint64_t value)
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

        void append_int64(std::string &out, std::int64_t value)
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

        void append_float(std::string &out, float value)
        {
            char buf[64];
            const auto result = std::to_chars(
                buf,
                buf + sizeof(buf),
                static_cast<double>(value),
                std::chars_format::fixed,
                6);
            if (result.ec == std::errc())
            {
                out.append(buf, static_cast<std::size_t>(result.ptr - buf));
                return;
            }
            out += std::to_string(value);
        }
    } // namespace

    DBPublisherTask::DBPublisherTask(InfluxHTTPClient &client,
                                     VoltageQueue &voltage_queue,
                                     TemperatureQueue &temperature_queue,
                                     DBPublisherConfig cfg)
        : client_(client),
          voltage_queue_(voltage_queue),
          temperature_queue_(temperature_queue),
          cfg_(cfg)
    {
    }

    void DBPublisherTask::operator()()
    {
        std::string payload;
        payload.reserve(cfg_.max_payload_bytes);

        std::size_t lines_in_payload = 0;
        VoltageCurrentSample *vc_ptr = nullptr;
        TemperatureSample *temp_ptr = nullptr;

        while (true)
        {
            if (voltage_queue_.try_pop(vc_ptr))
            {
            }
            else if (temperature_queue_.try_pop(temp_ptr))
            {
            }
            else
            {
                if (voltage_queue_.is_closed() && temperature_queue_.is_closed())
                {
                    break;
                }
                const bool got_voltage = voltage_queue_.wait_for_and_pop(vc_ptr, cfg_.flush_interval);
                if (!got_voltage)
                {
                    const bool temp_arrived = temperature_queue_.try_pop(temp_ptr);
                    if (temp_arrived)
                    {
                    }
                    else
                    {
                        (void)flush_payload_(payload, false);
                        continue;
                    }
                }
            }

            if (vc_ptr != nullptr && append_voltage_row_(payload, *vc_ptr))
            {
                diagnostics_.voltage_rows_written += 1;
                lines_in_payload += 1;
            }
            if (vc_ptr != nullptr)
            {
                voltage_queue_.dispose(vc_ptr);
                vc_ptr = nullptr;
            }

            if (temp_ptr != nullptr && append_temperature_row_(payload, *temp_ptr))
            {
                diagnostics_.temperature_rows_written += 1;
                lines_in_payload += 1;
            }
            if (temp_ptr != nullptr)
            {
                temperature_queue_.dispose(temp_ptr);
                temp_ptr = nullptr;
            }

            while (temperature_queue_.try_pop(temp_ptr))
            {
                if (append_temperature_row_(payload, *temp_ptr))
                {
                    diagnostics_.temperature_rows_written += 1;
                    lines_in_payload += 1;
                }
                temperature_queue_.dispose(temp_ptr);
                temp_ptr = nullptr;
            }

            while (voltage_queue_.try_pop(vc_ptr))
            {
                if (append_voltage_row_(payload, *vc_ptr))
                {
                    diagnostics_.voltage_rows_written += 1;
                    lines_in_payload += 1;
                }
                voltage_queue_.dispose(vc_ptr);
                vc_ptr = nullptr;
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

        while (temperature_queue_.try_pop(temp_ptr))
        {
            if (append_temperature_row_(payload, *temp_ptr))
            {
                diagnostics_.temperature_rows_written += 1;
                lines_in_payload += 1;
            }
            temperature_queue_.dispose(temp_ptr);
            temp_ptr = nullptr;
        }

        (void)flush_payload_(payload, false);
    }

    bool DBPublisherTask::append_voltage_row_(std::string &payload, const VoltageCurrentSample &sample)
    {
        payload += "voltage_current ";

        for (std::size_t i = 0; i < sample.cell_voltages.size(); ++i)
        {
            if (i > 0)
            {
                payload.push_back(',');
            }
            payload += "cell";
            append_uint64(payload, i + 1);
            payload += "_v=";
            append_float(payload, sample.cell_voltages[i]);
        }

        payload += ",raw_current_sensor_v=";
        append_float(payload, sample.raw_current_sensor_v);
        payload += ",current_a=";
        append_float(payload, sample.current_a);
        payload += ",sequence=";
        append_uint64(payload, sample.sequence);
        payload += "u ";
        append_int64(payload, to_influxdb_ns(sample.timestamp));
        payload.push_back('\n');
        return true;
    }

    bool DBPublisherTask::append_temperature_row_(std::string &payload, const TemperatureSample &sample)
    {
        payload += "temperature ";

        for (std::size_t i = 0; i < sample.temperatures.size(); ++i)
        {
            if (i > 0)
            {
                payload.push_back(',');
            }
            payload += "sensor";
            append_uint64(payload, i + 1);
            payload += "_c=";
            append_float(payload, sample.temperatures[i]);
        }

        payload += ",sequence=";
        append_uint64(payload, sample.sequence);
        payload += "u ";
        append_int64(payload, to_influxdb_ns(sample.timestamp));
        payload.push_back('\n');
        return true;
    }

    bool DBPublisherTask::flush_payload_(std::string &payload, bool threshold_flush)
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
