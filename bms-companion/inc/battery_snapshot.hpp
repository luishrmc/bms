#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace bms
{

    struct BatterySnapshot
    {
        static constexpr std::size_t kRegisterCount = 125;

        std::chrono::system_clock::time_point timestamp{};
        std::array<uint16_t, kRegisterCount> raw_registers{};

        float pack_voltage_v{0.0F};
        float pack_current_a{0.0F};
        bool current_scaled{false};

        std::array<uint16_t, 16> cell_voltage_mv{};
        int16_t bms_cooling_temp_c{0};
        int16_t battery_internal_temp_c{0};
        int16_t max_cell_temp_c{0};

        uint16_t remaining_capacity_ah{0};
        uint16_t max_charge_current_a{0};
        uint16_t soh_pct{0};
        uint16_t soc_pct{0};

        uint16_t status_raw{0};
        uint16_t alarm_raw{0};
        uint16_t protection_raw{0};
        uint16_t error_raw{0};

        uint32_t cycle_count{0};
        uint32_t full_charge_capacity_mas{0};

        uint16_t cell_count{0};
        float full_charge_capacity_ah{0.0F};

        std::string serial_or_model{""};
        std::string bms_version{""};
        std::string manufacturer{""};

        static uint32_t combine_u32(uint16_t high, uint16_t low) noexcept
        {
            return (static_cast<uint32_t>(high) << 16U) | static_cast<uint32_t>(low);
        }

        static std::string decode_status(uint16_t raw)
        {
            switch (raw)
            {
            case 0x0000:
                return "Standby";
            case 0x0001:
                return "Charging";
            case 0x0002:
                return "Discharging";
            case 0x0004:
                return "Protected";
            default:
                break;
            }

            std::ostringstream oss;
            bool first = true;

            auto append_flag = [&](bool cond, const char *name)
            {
                if (!cond)
                {
                    return;
                }
                if (!first)
                {
                    oss << " | ";
                }
                oss << name;
                first = false;
            };

            append_flag((raw & 0x0001U) != 0U, "Charging");
            append_flag((raw & 0x0002U) != 0U, "Discharging");
            append_flag((raw & 0x0004U) != 0U, "Protected");

            if (first)
            {
                oss << "Unknown(0x" << std::hex << std::uppercase << raw << ")";
            }

            return oss.str();
        }

        void print(std::ostream &os) const
        {
            const auto ts_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();

            os << "\n========== Battery Snapshot ==========\n";
            os << "timestamp_ms         : " << ts_ms << "\n";
            os << "pack_voltage_v       : " << std::fixed << std::setprecision(2) << pack_voltage_v << "\n";

            if (current_scaled)
            {
                os << "pack_current_a       : " << std::fixed << std::setprecision(3) << pack_current_a << "\n";
            }
            else
            {
                os << "pack_current_raw     : " << static_cast<int16_t>(raw_registers[1]) << " (scale not applied)\n";
            }

            os << "soc_pct              : " << soc_pct << "\n";
            os << "soh_pct              : " << soh_pct << "\n";
            os << "status               : " << decode_status(status_raw) << " [0x"
               << std::hex << std::uppercase << status_raw << std::dec << "]\n";
            os << "alarm_raw            : 0x" << std::hex << std::uppercase << alarm_raw << std::dec << "\n";
            os << "protection_raw       : 0x" << std::hex << std::uppercase << protection_raw << std::dec << "\n";
            os << "error_raw            : 0x" << std::hex << std::uppercase << error_raw << std::dec << "\n";
            os << "bms_cooling_temp_c   : " << bms_cooling_temp_c << "\n";
            os << "battery_internal_c   : " << battery_internal_temp_c << "\n";
            os << "max_cell_temp_c      : " << max_cell_temp_c << "\n";
            os << "remaining_capacity_ah: " << remaining_capacity_ah << "\n";
            os << "max_charge_current_a : " << max_charge_current_a << "\n";
            os << "cycle_count          : " << cycle_count << "\n";
            os << "full_charge_cap_ah   : " << std::fixed << std::setprecision(1) << full_charge_capacity_ah << "\n";
            os << "cell_count           : " << cell_count << "\n";

            os << "cell_voltage_mv      : ";
            for (std::size_t i = 0; i < cell_voltage_mv.size(); ++i)
            {
                if (i != 0U)
                {
                    os << ", ";
                }
                os << "c" << (i + 1U) << "=" << cell_voltage_mv[i];
            }
            os << "\n";

            if (!serial_or_model.empty())
            {
                os << "serial_or_model      : " << serial_or_model << "\n";
            }
            if (!bms_version.empty())
            {
                os << "bms_version          : " << bms_version << "\n";
            }
            if (!manufacturer.empty())
            {
                os << "manufacturer         : " << manufacturer << "\n";
            }

            os << "======================================\n";
        }
    };

} // namespace bms