#include "snapshot_to_lp.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <string>

namespace bms
{
    namespace
    {
        inline std::int64_t to_influxdb_ns(std::chrono::system_clock::time_point tp) noexcept
        {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(tp.time_since_epoch()).count();
        }

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

        void append_int32(std::string &out, std::int32_t value)
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

        void append_escaped_string_field(std::string &out, const std::string &value)
        {
            out.push_back('"');
            for (const char ch : value)
            {
                if (ch == '"' || ch == '\\')
                {
                    out.push_back('\\');
                }
                out.push_back(ch);
            }
            out.push_back('"');
        }

    } // namespace

    bool SnapshotToLP::append_battery_snapshot_row(std::string &payload,
                                                   const BatterySnapshot &snapshot)
    {
        payload += "battery_snapshot ";

        payload += "pack_voltage_v=";
        append_float(payload, snapshot.pack_voltage_v);

        if (snapshot.current_scaled)
        {
            payload += ",pack_current_a=";
            append_float(payload, snapshot.pack_current_a);
        }
        else
        {
            payload += ",pack_current_raw=";
            append_int32(payload, static_cast<std::int16_t>(snapshot.raw_registers[1]));
            payload += 'i';
        }

        payload += ",soc_pct=";
        append_uint64(payload, snapshot.soc_pct);
        payload += "u";

        payload += ",soh_pct=";
        append_uint64(payload, snapshot.soh_pct);
        payload += "u";

        payload += ",remaining_capacity_ah=";
        append_uint64(payload, snapshot.remaining_capacity_ah);
        payload += "u";

        payload += ",max_charge_current_a=";
        append_uint64(payload, snapshot.max_charge_current_a);
        payload += "u";

        payload += ",bms_cooling_temp_c=";
        append_int32(payload, snapshot.bms_cooling_temp_c);
        payload += 'i';

        payload += ",battery_internal_temp_c=";
        append_int32(payload, snapshot.battery_internal_temp_c);
        payload += 'i';

        payload += ",max_cell_temp_c=";
        append_int32(payload, snapshot.max_cell_temp_c);
        payload += 'i';

        payload += ",status_raw=";
        append_uint64(payload, snapshot.status_raw);
        payload += "u";

        payload += ",alarm_raw=";
        append_uint64(payload, snapshot.alarm_raw);
        payload += "u";

        payload += ",protection_raw=";
        append_uint64(payload, snapshot.protection_raw);
        payload += "u";

        payload += ",error_raw=";
        append_uint64(payload, snapshot.error_raw);
        payload += "u";

        payload += ",cycle_count=";
        append_uint64(payload, snapshot.cycle_count);
        payload += "u";

        payload += ",full_charge_capacity_mas=";
        append_uint64(payload, snapshot.full_charge_capacity_mas);
        payload += "u";

        // payload += ",cell_count=";
        // append_uint64(payload, snapshot.cell_count);
        // payload += "u";

        payload += ",full_charge_capacity_ah=";
        append_float(payload, snapshot.full_charge_capacity_ah);

        for (std::size_t i = 0; i < snapshot.cell_voltage_mv.size(); ++i)
        {
            payload += ",cell";
            append_uint64(payload, i + 1U);
            payload += "_mv=";
            append_uint64(payload, snapshot.cell_voltage_mv[i]);
            payload += "u";
        }

        // if (!snapshot.serial_or_model.empty())
        // {
        //     payload += ",serial_or_model=";
        //     append_escaped_string_field(payload, snapshot.serial_or_model);
        // }

        // if (!snapshot.bms_version.empty())
        // {
        //     payload += ",bms_version=";
        //     append_escaped_string_field(payload, snapshot.bms_version);
        // }

        // if (!snapshot.manufacturer.empty())
        // {
        //     payload += ",manufacturer=";
        //     append_escaped_string_field(payload, snapshot.manufacturer);
        // }

        payload.push_back(' ');
        append_int64(payload, to_influxdb_ns(snapshot.timestamp));
        payload.push_back('\n');

        return true;
    }

} // namespace bms