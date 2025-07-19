/**
 * @file        data_logger_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Implementation.
 * @version     0.0.2
 * @date        2025-07-18
 *               _   _  _____  __  __   _____
 *              | | | ||  ___||  \/  | / ____|
 *              | | | || |_   | \  / || |  __
 *              | | | ||  _|  | |\/| || | |_ |
 *              | |_| || |    | |  | || |__| |
 *               \___/ |_|    |_|  |_| \_____|
 *
 *            Universidade Federal de Minas Gerais
 *                DELT · BMS Project
 */

// ----------------------------- Includes ----------------------------- //

#include "data_logger_service.hpp"
#include "logging_service.hpp"
#include <thread>
#include <iostream>
#include <cstring>

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

DataLoggerService::DataLoggerService(const std::string &ip, uint16_t port, uint8_t s_id)
    : _ip(ip), _port(port), _s_id(s_id), _connected(false),
      _mb(std::make_unique<modbus>(ip, port))
{
    _mb->modbus_set_slave_id(s_id);
}

bool DataLoggerService::connect(int max_attempts, int retry_delay_ms)
{
    if (_connected)
    {
        log(LogLevel::Warn, "Already connected, skipping reconnect.");
        return true;
    }
    if (!_mb)
    {
        log(LogLevel::Error, "Modbus client not initialized.");
        return false;
    }

    log(LogLevel::Info, fmt::format("Attempting to connect to {}:{}", _ip, _port));
    for (int attempt = 1; attempt <= max_attempts; ++attempt)
    {
        log(LogLevel::Info, fmt::format("Connect attempt {}/{}", attempt, max_attempts));
        if (_mb->modbus_connect())
        {
            _connected = true;
            log(LogLevel::Info, fmt::format("Connected to Data Logger at {}:{}", _ip, _port));
            return true;
        }
        if (attempt < max_attempts)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }
    }
    _connected = false;
    log(LogLevel::Error, fmt::format("Connection failed after all attempts to {}:{}", _ip, _port));
    return false;
}

void DataLoggerService::disconnect()
{
    if (_connected && _mb)
    {
        _mb->modbus_close();
        _connected = false;
        log(LogLevel::Info, fmt::format("Disconnected from {}:{}", _ip, _port));
    }
    else
    {
        log(LogLevel::Warn, fmt::format("Attempted to disconnect from {}:{}, but was not connected.", _ip, _port));
    }
}

// ─────────────── section-0 (RO) ───────────────
bool DataLoggerService::read_status()
{
    uint16_t w;
    if (!read_input_u16(REG_STATUS, w))
        return false;
    _mode = static_cast<Mode>(w & 0x007F);
    _ntp = bool((w >> 14) & 0x1);
    _autocal = bool((w >> 15) & 0x1);
    return true;
}
bool DataLoggerService::read_active_sampling() { return read_input_u32(REG_ACT_SAMPLING, _sampling_period); }
bool DataLoggerService::read_channel(uint8_t ch) { return ch < 16 && read_input_f32(REG_CH_BASE + ch * 2u, _adc_channels[ch]) ? true : false; }
bool DataLoggerService::read_board_temp() { return read_input_u16(REG_BOARD_TEMP, _board_temp); }
bool DataLoggerService::read_board_uid() { return read_input_u32(REG_BOARD_UID, _board_uid); }
bool DataLoggerService::read_firmware()
{
    uint16_t ver;
    if (!read_input_u16(REG_FW_VERSION, ver) || !read_input_u16(REG_FW_BUILD, _fw_build))
        return false;
    _fw_major = static_cast<uint8_t>(ver >> 8);
    _fw_minor = static_cast<uint8_t>(ver & 0xFF);
    return true;
}

// ─────────────── section-1 (RO) ───────────────
bool DataLoggerService::read_act_adc_scale(uint8_t ch) { return ch < 8 && read_input_f32(REG_ACT_ADC_SCALE + ch * 2u, _adc_scales[ch]) ? true : false; }
bool DataLoggerService::read_act_transd_scale(uint8_t ch) { return ch < 16 && read_input_f32(REG_ACT_TRANSD_SCALE + ch * 2u, _transd_scales[ch]) ? true : false; }
bool DataLoggerService::read_act_transd_offset(uint8_t ch) { return ch < 8 && read_input_f32(REG_ACT_TRANSD_OFFS + ch * 2u, _transd_offsets[ch]) ? true : false; }
bool DataLoggerService::read_act_pga_gain(uint8_t ch) { return ch < 8 && read_input_u16(REG_ACT_PGA_GAIN + ch, _pga_gains[ch]) ? true : false; }

// ─────────────── section-2 setters ───────────────
bool DataLoggerService::write_adc_scale(uint8_t ch, float val)
{
    if (_connected && ch < 8 && write_f32(REG_SET_ADC_SCALE + ch * 2u, val))
    {
        _adc_scales[ch] = val;
        log(LogLevel::Info, fmt::format("ADC scale for channel {} set to {}", ch, val));
        return true;
    }
    return false;
}

bool DataLoggerService::write_transd_scale(uint8_t ch, float val)
{
    if (_connected && ch < 16 && write_f32(REG_SET_TRANSD_SCALE + ch * 2u, val))
    {
        _transd_scales[ch] = val;
        log(LogLevel::Info, fmt::format("Transducer scale for channel {} set to {}", ch, val));
        return true;
    }
    return false;
}

bool DataLoggerService::write_transd_offset(uint8_t ch, float val)
{
    if (_connected && ch < 8 && write_f32(REG_SET_TRANSD_OFFS + ch * 2u, val))
    {
        _transd_offsets[ch] = val;
        log(LogLevel::Info, fmt::format("Transducer offset for channel {} set to {}", ch, val));
        return true;
    }
    return false;
}

bool DataLoggerService::write_pga_gain(uint8_t ch, uint16_t val)
{
    if (_connected && ch < 8 && write_u16(REG_SET_PGA_GAIN + ch, val))
    {
        _pga_gains[ch] = val;
        log(LogLevel::Info, fmt::format("PGA gain for channel {} set to {}", ch, val));
        return true;
    }
    return false;
}

bool DataLoggerService::write_sampling_period(uint32_t us)
{
    if (_connected && write_u32(REG_SET_SAMPLING, us))
    {
        _sampling_period = us;
        log(LogLevel::Info, fmt::format("Sampling period set to {} µs", us));
        return true;
    }
    return false;
}

// ─────────────── section-3 system control ───────────────
bool DataLoggerService::write_password(uint32_t pwd) { return write_u32(REG_PASSWORD, pwd); }
bool DataLoggerService::read_password(uint32_t &pwd) const { return read_holding_u32(REG_PASSWORD, pwd); }

bool DataLoggerService::send_command(Command cmd) { return write_u16(REG_COMMAND, static_cast<uint16_t>(cmd)); }

bool DataLoggerService::write_static_ip(uint32_t ip_be) { return write_u32(REG_STATIC_IP, ip_be); }
bool DataLoggerService::read_static_ip(uint32_t &ip_be) const { return read_holding_u32(REG_STATIC_IP, ip_be); }

// ───────────────────── generic helpers for read holding registers ─────────────────────
bool DataLoggerService::read_holding_u16(uint16_t addr, uint16_t &dst) const
{
    uint16_t tmp{};
    int rc = _connected ? _mb->modbus_read_holding_registers(addr, 1, &tmp) : -1;
    if (rc == 0)
    {
        dst = tmp;
        return true;
    }
    log(LogLevel::Error, fmt::format("read_holding_u16 failed at 0x{:04X} rc={}", addr, rc));
    return false;
}

bool DataLoggerService::read_holding_u32(uint16_t addr, uint32_t &dst) const
{
    uint16_t buf[2]{};
    int rc = _connected ? _mb->modbus_read_holding_registers(addr, 2, buf) : -1;
    if (rc == 0)
    {
        dst = (uint32_t(buf[0]) << 16) | buf[1];
        return true;
    }
    log(LogLevel::Error, fmt::format("read_holding_u32 failed at 0x{:04X} rc={}", addr, rc));
    return false;
}

bool DataLoggerService::read_holding_f32(uint16_t addr, float &dst) const
{
    uint32_t u32;
    return read_holding_u32(addr, u32) ? (std::memcpy(&dst, &u32, 4), true) : false;
}

// ───────────────────── generic helpers for read input registers ─────────────────────
bool DataLoggerService::read_input_u16(uint16_t addr, uint16_t &dst) const
{
    uint16_t tmp{};
    int rc = _connected ? _mb->modbus_read_input_registers(addr, 1, &tmp) : -1;
    if (rc == 0)
    {
        dst = tmp;
        return true;
    }
    log(LogLevel::Error, fmt::format("read_input_u16 failed at 0x{:04X} rc={}", addr, rc));
    return false;
}

bool DataLoggerService::read_input_u32(uint16_t addr, uint32_t &dst) const
{
    uint16_t buf[2]{};
    int rc = _connected ? _mb->modbus_read_input_registers(addr, 2, buf) : -1;
    if (rc == 0)
    {
        dst = (uint32_t(buf[0]) << 16) | buf[1];
        return true;
    }
    log(LogLevel::Error, fmt::format("read_input_u32 failed at 0x{:04X} rc={}", addr, rc));
    return false;
}

bool DataLoggerService::read_input_f32(uint16_t addr, float &dst) const
{
    uint32_t u32;
    return read_input_u32(addr, u32) ? (std::memcpy(&dst, &u32, 4), true) : false;
}

// ───────────────────── generic helpers for write ─────────────────────
bool DataLoggerService::write_u16(uint16_t addr, uint16_t v)
{
    int rc = _connected ? _mb->modbus_write_register(addr, v) : -1;
    if (rc == 0)
        return true;
    log(LogLevel::Error, fmt::format("write_holding_u16 failed at 0x{:04X} rc={}", addr, rc));
    return false;
}

bool DataLoggerService::write_u32(uint16_t addr, uint32_t v)
{
    uint16_t buf[2]{uint16_t(v >> 16), uint16_t(v)};
    int rc = _connected ? _mb->modbus_write_registers(addr, 2, buf) : -1;
    if (rc == 0)
        return true;
    log(LogLevel::Error, fmt::format("write_holding_u32 failed at 0x{:04X} rc={}", addr, rc));
    return false;
}

bool DataLoggerService::write_f32(uint16_t addr, float f)
{
    uint32_t u32;
    std::memcpy(&u32, &f, 4);
    return write_u32(addr, u32);
}

// *********************** END OF FILE ******************************* //
