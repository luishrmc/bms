/**
 * @file        data_logger_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Implementation.
 * @version     0.1.0
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

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

DataLoggerService::DataLoggerService(const std::string &ip, uint16_t port, uint8_t s_id)
    : ModBusService(ip, port, s_id)
{
    // Initialize ADC channels to zero
    std::fill(std::begin(_adc_channels), std::end(_adc_channels), 0.0f);
}

std::future<bool> DataLoggerService::connect(uint8_t max_attempts)
{
    return std::async(
        std::launch::async,
        [this, max_attempts]() -> bool
        {
            if (!ModBusService::connect(max_attempts).get())
                return false;
            read_status();
            read_firmware_version();
            read_board_uid();
            return true;
        });
}

std::future<bool> DataLoggerService::disconnect()
{
    return ModBusService::disconnect();
}

bool DataLoggerService::read_status()
{
    uint16_t w;
    if (!ModBusService::read_u16(REG_STATUS, w).get())
        return false;
    _mode = static_cast<Mode>(w & 0x007F);
    _ntp = bool((w >> 14) & 0x1);
    _autocal = bool((w >> 15) & 0x1);
    return true;
}

bool DataLoggerService::read_active_sampling() { return ModBusService::read_u32(REG_ACT_SAMPLING, _sampling_period).get(); }
bool DataLoggerService::read_channel(uint8_t ch) { return ch < 16 && ModBusService::read_f32(REG_CH_BASE + ch * 2u, _adc_channels[ch]).get(); }
bool DataLoggerService::read_board_temp() { return ModBusService::read_u16(REG_BOARD_TEMP, _board_temp).get(); }
bool DataLoggerService::read_board_uid() { return ModBusService::read_u32(REG_BOARD_UID, _board_uid).get(); }
bool DataLoggerService::read_firmware_version()
{
    uint16_t ver;
    if (!ModBusService::read_u16(REG_FW_VERSION, ver).get() || !ModBusService::read_u16(REG_FW_BUILD, _fw_build).get())
        return false;
    _fw_major = static_cast<uint8_t>(ver >> 8);
    _fw_minor = static_cast<uint8_t>(ver & 0xFF);
    return true;
}

/* ------------------- Section 1 (read active configuration) ------------------- */
bool DataLoggerService::read_act_adc_scale(uint8_t ch) { return ch < 8 && ModBusService::read_f32(REG_ACT_ADC_SCALE + ch * 2u, _adc_scales[ch]).get(); }
bool DataLoggerService::read_act_transd_scale(uint8_t ch) { return ch < 16 && ModBusService::read_f32(REG_ACT_TRANSD_SCALE + ch * 2u, _transd_scales[ch]).get(); }
bool DataLoggerService::read_act_transd_offset(uint8_t ch) { return ch < 8 && ModBusService::read_f32(REG_ACT_TRANSD_OFFS + ch * 2u, _transd_offsets[ch]).get(); }
bool DataLoggerService::read_act_pga_gain(uint8_t ch) { return ch < 8 && ModBusService::read_u16(REG_ACT_PGA_GAIN + ch, _pga_gains[ch]).get(); }

/* ------------------- Section 2 (set active configuration) ------------------- */
bool DataLoggerService::write_adc_scale(uint8_t ch, float val)
{
    if (ModBusService::_connected &&
        ch < 8 && ModBusService::write_f32(REG_SET_ADC_SCALE + ch * 2u, val).get())
    {
        _adc_scales[ch] = val;
        return true;
    }
    return false;
}

bool DataLoggerService::write_transd_scale(uint8_t ch, float val)
{
    if (ModBusService::_connected &&
        ch < 16 && ModBusService::write_f32(REG_SET_TRANSD_SCALE + ch * 2u, val).get())
    {
        _transd_scales[ch] = val;
        return true;
    }
    return false;
}

bool DataLoggerService::write_transd_offset(uint8_t ch, float val)
{
    if (ModBusService::_connected &&
        ch < 8 && ModBusService::write_f32(REG_SET_TRANSD_OFFS + ch * 2u, val).get())
    {
        _transd_offsets[ch] = val;
        return true;
    }
    return false;
}

bool DataLoggerService::write_pga_gain(uint8_t ch, uint16_t val)
{
    if (ModBusService::_connected &&
        ch < 8 && ModBusService::write_u16(REG_SET_PGA_GAIN + ch, val).get())
    {
        _pga_gains[ch] = val;
        return true;
    }
    return false;
}

bool DataLoggerService::write_sampling_period(uint32_t us)
{
    if (ModBusService::_connected && ModBusService::write_u32(REG_SET_SAMPLING, us).get())
    {
        _sampling_period = us;
        return true;
    }
    return false;
}

/* --------------------------- Section 3 (system control) --------------------------- */
bool DataLoggerService::write_password(uint32_t pwd) { return ModBusService::write_u32(REG_PASSWORD, pwd).get(); }
bool DataLoggerService::read_password(uint32_t &pwd) { return ModBusService::read_u32(REG_PASSWORD, pwd).get(); }
bool DataLoggerService::send_command(Command cmd)
{
    uint16_t value = static_cast<uint16_t>(cmd);
    return ModBusService::write_u16(REG_COMMAND, value).get();
}
bool DataLoggerService::write_static_ip(uint32_t ip_be) { return ModBusService::write_u32(REG_STATIC_IP, ip_be).get(); }
bool DataLoggerService::read_static_ip(uint32_t &ip_be) { return ModBusService::read_u32(REG_STATIC_IP, ip_be).get(); }

std::future<bool> DataLoggerService::read_all_channels()
{
    return std::async(
        std::launch::async,
        [this]() -> bool
        {
            uint16_t buf[32]{};
            if (read_raw(REG_CH_BASE, 32, buf).get())
            {
                for (uint16_t i = 0; i < 15; ++i)
                {
                    uint32_t bits = (uint32_t(buf[2 * i]) << 16) | buf[2 * i + 1];
                    float ch = std::bit_cast<float>(bits);
                    _adc_channels[i] = ch;
                }
                return true;
            }
            return false;
        });
}

std::future<bool> DataLoggerService::read_all_config()
{
    read_active_sampling();
    uint16_t buf[187]{};
    return ModBusService::read_raw(REG_CH_BASE, 187, buf);
}

// // *********************** END OF FILE ******************************* //
