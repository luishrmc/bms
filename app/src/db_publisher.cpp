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

    void DBPublisherTask::operator()()
    {
        const MeasurementFrame frame = bus_.latest();

        if (frame.voltage_current.has_value())
        {
            const auto &sample = frame.voltage_current.value();
            if (!last_voltage_sequence_.has_value() || sample.sequence > last_voltage_sequence_.value())
            {
                if (publish_voltage_current_(sample))
                {
                    last_voltage_sequence_ = sample.sequence;
                }
            }
        }

        if (frame.temperature.has_value())
        {
            const auto &sample = frame.temperature.value();
            if (!last_temperature_sequence_.has_value() || sample.sequence > last_temperature_sequence_.value())
            {
                if (publish_temperature_(sample))
                {
                    last_temperature_sequence_ = sample.sequence;
                }
            }
        }
    }

    bool DBPublisherTask::publish_voltage_current_(const VoltageCurrentSample &sample)
    {
        std::string payload;
        payload.reserve(512);
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

        std::string error;
        if (!client_.write_lp(payload, error))
        {
            diagnostics_.write_failures += 1;
            diagnostics_.last_error = std::move(error);
            return false;
        }

        diagnostics_.voltage_rows_written += 1;
        return true;
    }

    bool DBPublisherTask::publish_temperature_(const TemperatureSample &sample)
    {
        std::string payload;
        payload.reserve(512);
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

        std::string error;
        if (!client_.write_lp(payload, error))
        {
            diagnostics_.write_failures += 1;
            diagnostics_.last_error = std::move(error);
            return false;
        }

        diagnostics_.temperature_rows_written += 1;
        return true;
    }

} // namespace bms
