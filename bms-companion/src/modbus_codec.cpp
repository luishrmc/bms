#include "modbus_codec.hpp"

#include <modbus/modbus.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace bms
{

/**
 * @brief Builds a codec bound to one fixed RS485/Modbus configuration.
 * @param cfg Serial port and register-map settings.
 */
ModbusCodec::ModbusCodec(const RS485Config &cfg)
    : cfg_(cfg)
{
}

ModbusCodec::~ModbusCodec()
{
    disconnect();
}

bool ModbusCodec::connect()
{
    disconnect();

    ctx_ = modbus_new_rtu(
        cfg_.device.c_str(),
        cfg_.baudrate,
        cfg_.parity,
        cfg_.data_bits,
        cfg_.stop_bits);

    if (ctx_ == nullptr)
    {
        return false;
    }

    if (modbus_set_slave(ctx_, cfg_.slave_id) != 0)
    {
        disconnect();
        return false;
    }

    if (modbus_set_response_timeout(ctx_, cfg_.response_timeout_sec, cfg_.response_timeout_usec) != 0)
    {
        disconnect();
        return false;
    }

    if (modbus_connect(ctx_) != 0)
    {
        disconnect();
        return false;
    }

    return true;
}

void ModbusCodec::disconnect()
{
    if (ctx_ != nullptr)
    {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
}

bool ModbusCodec::is_connected() const noexcept
{
    return ctx_ != nullptr;
}

int16_t ModbusCodec::as_int16(uint16_t raw) noexcept
{
    return static_cast<int16_t>(raw);
}

std::string ModbusCodec::decode_ascii_block(
    const std::array<uint16_t, BatterySnapshot::kRegisterCount> &regs,
    std::size_t start,
    std::size_t end)
{
    std::string out;
    out.reserve((end - start + 1U) * 2U);

    for (std::size_t i = start; i <= end; ++i)
    {
        const uint16_t reg = regs[i];
        const char hi = static_cast<char>((reg >> 8U) & 0xFFU);
        const char lo = static_cast<char>(reg & 0xFFU);

        if (hi != '\0')
        {
            out.push_back(hi);
        }
        if (lo != '\0')
        {
            out.push_back(lo);
        }
    }

    while (!out.empty() && (out.back() == ' ' || out.back() == '\t'))
    {
        out.pop_back();
    }

    return out;
}

/**
 * @brief Reads one holding-register block and decodes battery engineering data.
 * @param out_snapshot Destination snapshot.
 * @param error_out Error detail when decoding fails.
 * @return True on success.
 * @note Current scaling is only applied when `current_scale_a_per_lsb > 0`.
 */
bool ModbusCodec::read_snapshot(BatterySnapshot &out_snapshot, std::string &error_out)
{
    if (ctx_ == nullptr)
    {
        error_out = "Modbus RTU context is not connected";
        return false;
    }

    if (cfg_.read_register_count <= 0 ||
        cfg_.read_register_count > static_cast<int>(BatterySnapshot::kRegisterCount))
    {
        error_out = "Invalid register count configuration";
        return false;
    }

    std::vector<uint16_t> regs(static_cast<std::size_t>(cfg_.read_register_count), 0U);

    const int rc = modbus_read_registers(
        ctx_,
        cfg_.read_start_address,
        cfg_.read_register_count,
        regs.data());

    if (rc != cfg_.read_register_count)
    {
        error_out = modbus_strerror(errno);
        return false;
    }

    out_snapshot = BatterySnapshot{};
    out_snapshot.timestamp = std::chrono::system_clock::now();

    for (int i = 0; i < cfg_.read_register_count; ++i)
    {
        out_snapshot.raw_registers[static_cast<std::size_t>(i)] = regs[static_cast<std::size_t>(i)];
    }

    // Core fields from the documented register map.
    out_snapshot.pack_voltage_v = static_cast<float>(out_snapshot.raw_registers[0]) * 0.01F;

    if (cfg_.current_scale_a_per_lsb > 0.0F)
    {
        out_snapshot.pack_current_a =
            static_cast<float>(as_int16(out_snapshot.raw_registers[1])) * cfg_.current_scale_a_per_lsb;
        out_snapshot.current_scaled = true;
    }

    for (std::size_t i = 0; i < out_snapshot.cell_voltage_mv.size(); ++i)
    {
        out_snapshot.cell_voltage_mv[i] = out_snapshot.raw_registers[2U + i];
    }

    out_snapshot.bms_cooling_temp_c = as_int16(out_snapshot.raw_registers[18]);
    out_snapshot.battery_internal_temp_c = as_int16(out_snapshot.raw_registers[19]);
    out_snapshot.max_cell_temp_c = as_int16(out_snapshot.raw_registers[20]);

    out_snapshot.remaining_capacity_ah = out_snapshot.raw_registers[21];
    out_snapshot.max_charge_current_a = out_snapshot.raw_registers[22];
    out_snapshot.soh_pct = out_snapshot.raw_registers[23];
    out_snapshot.soc_pct = out_snapshot.raw_registers[24];

    out_snapshot.status_raw = out_snapshot.raw_registers[25];
    out_snapshot.alarm_raw = out_snapshot.raw_registers[26];
    out_snapshot.protection_raw = out_snapshot.raw_registers[27];
    out_snapshot.error_raw = out_snapshot.raw_registers[28];

    out_snapshot.cycle_count =
        BatterySnapshot::combine_u32(out_snapshot.raw_registers[29], out_snapshot.raw_registers[30]);

    out_snapshot.full_charge_capacity_mas =
        BatterySnapshot::combine_u32(out_snapshot.raw_registers[31], out_snapshot.raw_registers[32]);

    out_snapshot.cell_count = out_snapshot.raw_registers[36];
    out_snapshot.full_charge_capacity_ah =
        static_cast<float>(out_snapshot.raw_registers[37]) * 0.1F;

    out_snapshot.serial_or_model = decode_ascii_block(out_snapshot.raw_registers, 105, 119);
    out_snapshot.bms_version = decode_ascii_block(out_snapshot.raw_registers, 117, 119);
    out_snapshot.manufacturer = decode_ascii_block(out_snapshot.raw_registers, 120, 124);

    return true;
}

} // namespace bms
