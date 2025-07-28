/**
 * @file        data_logger_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Implementation.
 * @version     0.2.1
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
#include "config.hpp"

// -------------------------- Private Types --------------------------- //

using dl_err_t = DataLoggerService::dl_err_t;
using json = nlohmann::json;
// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

DataLoggerService::DataLoggerService(const std::string &ip, uint16_t port, uint8_t s_id)
    : modbus(ip, port)
{
    modbus_set_slave_id(s_id);
}

bool DataLoggerService::connect(uint8_t max_attempts)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    for (uint8_t i = 0; i < max_attempts; ++i)
    {
        if (is_connected())
            return true;
        else
        {
            app_log(LogLevel::Info, fmt::format("Connecting to Data Logger {}/{}", i + 1, max_attempts));
            if (modbus_connect())
            {
                app_log(LogLevel::Info, "Connected to Data Logger");
                _is_linked = false;
                return true;
            }
        }
    }
    return false;
}

void DataLoggerService::disconnect()
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    modbus_close();
}

int DataLoggerService::read_status()
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t status;
    int err = modbus_read_input_registers(REG_STATUS, 1, &status);
    if (err == 0)
    {
        _mode = status & 0x007F;
        _ntp = bool((status >> 14) & 0x1);
        _autocal = bool((status >> 15) & 0x1);
    }
    return err;
}

int DataLoggerService::read_act_sampling()
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buff[2] = {0};
    int err = modbus_read_input_registers(REG_ACT_SAMPLING, 2, buff);
    if (err == 0)
        _sampling_period = (buff[0] << 16 | buff[1]);
    return err;
}

int DataLoggerService::read_channel(uint8_t ch)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 16)
    {
        uint16_t buff[2];
        int err = modbus_read_input_registers(REG_CH_BASE + ch * 2u, 2, buff);
        if (err == 0)
        {
            _adc_channels[ch] = std::bit_cast<float>(buff[0] << 16 | buff[1]);
        }
        return err;
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::read_board_temp()
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t temp;
    int err = modbus_read_input_registers(REG_BOARD_TEMP, 1, &temp);
    if (err == 0)
        _board_temp = temp;
    return err;
}

int DataLoggerService::read_board_uid()
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buffer[2];
    int err = modbus_read_input_registers(REG_BOARD_UID, 2, buffer);
    if (err == 0)
    {
        _board_uid = fmt::format("{:04X}{:04X}", buffer[0], buffer[1]);
    }
    return err;
}

int DataLoggerService::read_firmware_version()
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buffer[2];
    int err = modbus_read_input_registers(REG_FW_VERSION, 2, buffer);
    if (err == 0)
    {
        uint8_t fw_major = static_cast<uint8_t>(buffer[0] >> 8);
        uint8_t fw_minor = static_cast<uint8_t>(buffer[0] & 0xFF);
        _fw_version = fmt::format("{}.{}", fw_major, fw_minor);
        _fw_build = buffer[1];
    }
    return err;
}

/* ------------------- Section 1 (read active configuration) ------------------- */
int DataLoggerService::read_act_adc_scale(uint8_t ch)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 8)
    {
        return read_float(ch, REG_ACT_ADC_SCALE, _adc_scales[ch]);
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::read_act_transd_scale(uint8_t ch)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 16)
    {
        return read_float(ch, REG_ACT_TRANSD_SCALE, _transd_scales[ch]);
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::read_act_transd_offset(uint8_t ch)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 8)
    {
        return read_float(ch, REG_ACT_TRANSD_OFFS, _transd_offsets[ch]);
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::read_act_pga_gain(uint8_t ch)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 8)
        return modbus_read_input_registers(REG_ACT_PGA_GAIN + ch, 1, &_pga_gains[ch]);
    return EX_ILLEGAL_VALUE;
}

/* ------------------- Section 2 (set active configuration) ------------------- */
int DataLoggerService::write_adc_scale(uint8_t ch, float val)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 8)
    {
        int err = write_float(ch, REG_SET_ADC_SCALE, val);
        if (err == 0)
            _adc_scales[ch] = val;
        return err;
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::write_transd_scale(uint8_t ch, float val)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 16)
    {
        int err = write_float(ch, REG_SET_TRANSD_SCALE, val);
        if (err == 0)
            _transd_scales[ch] = val;
        return err;
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::write_transd_offset(uint8_t ch, float val)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 8)
    {
        int err = write_float(ch, REG_SET_TRANSD_OFFS, val);
        if (err == 0)
            _transd_offsets[ch] = val;
        return err;
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::write_pga_gain(uint8_t ch, uint16_t val)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    if (ch < 8)
    {
        int err = modbus_write_register(REG_SET_PGA_GAIN + ch, val);
        if (err == 0)
            _pga_gains[ch] = val;
        return err;
    }
    return EX_ILLEGAL_VALUE;
}

int DataLoggerService::write_sampling_period(uint32_t us)
{
    uint16_t buff[2] = {static_cast<uint16_t>(us >> 16), static_cast<uint16_t>(us & 0xFFFF)};
    int err = modbus_write_registers(REG_SET_SAMPLING, 2, buff);
    if (err == 0)
        _sampling_period = us;
    return err;
}

// /* --------------------------- Section 3 (system control) --------------------------- */

int DataLoggerService::write_password(uint32_t pwd)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buffer[2] = {static_cast<uint16_t>(pwd >> 16), static_cast<uint16_t>(pwd & 0xFFFF)};
    return modbus_write_registers(REG_PASSWORD, 2, buffer) == 0;
}

int DataLoggerService::read_password(uint32_t &pwd)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buffer[2];
    if (modbus_read_holding_registers(REG_PASSWORD, 2, buffer) == 0)
    {
        pwd = (static_cast<uint32_t>(buffer[0]) << 16) | buffer[1];
        return 0;
    }
    return -1;
}

int DataLoggerService::send_command(Command cmd)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t value = static_cast<uint16_t>(cmd);
    return modbus_write_register(REG_COMMAND, value);
}

int DataLoggerService::write_static_ip(uint32_t ip_be)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buffer[2] = {static_cast<uint16_t>(ip_be >> 16), static_cast<uint16_t>(ip_be & 0xFFFF)};
    return modbus_write_registers(REG_STATIC_IP, 2, buffer);
}

int DataLoggerService::read_static_ip(uint32_t &ip_be)
{
    std::lock_guard<std::mutex> lock(_mb_mtx);
    uint16_t buffer[2];
    err = modbus_read_holding_registers(REG_STATIC_IP, 2, buffer);
    if (err == 0)
    {
        ip_be = (static_cast<uint32_t>(buffer[0]) << 16) | buffer[1];
        return err;
    }
    return err;
}

int DataLoggerService::read_all_channels()
{
    uint16_t buff[32] = {0};
    std::lock_guard<std::mutex> lock(_mb_mtx);
    int err = modbus_read_input_registers(REG_CH_BASE, 32, buff);
    if (err == 0)
        for (uint8_t ch = 0; ch < 16; ++ch)
            _adc_channels[ch] = std::bit_cast<float>(buff[ch * 2] << 16 | buff[ch * 2 + 1]);
    return err;
}

int DataLoggerService::read_all_config()
{
    uint16_t buff[88] = {0};
    read_status();
    read_act_sampling();
    read_board_uid();
    read_firmware_version();

    std::lock_guard<std::mutex> lock(_mb_mtx);
    err = modbus_read_input_registers(REG_ACT_ADC_SCALE, 88, buff);
    if (err == 0)
    {
        for (uint8_t addr = REG_ACT_ADC_SCALE; addr < REG_ACT_TRANSD_SCALE; addr += 2)
        {
            uint8_t ch = (addr - REG_ACT_ADC_SCALE) / 2;
            _adc_scales[ch] = std::bit_cast<float>(buff[addr - REG_ACT_ADC_SCALE] << 16 | buff[addr - REG_ACT_ADC_SCALE + 1]);
        }
        for (uint8_t addr = REG_ACT_TRANSD_SCALE; addr < REG_ACT_TRANSD_OFFS; addr += 2)
        {
            uint8_t ch = (addr - REG_ACT_TRANSD_SCALE) / 2;
            _transd_scales[ch] = std::bit_cast<float>(buff[addr - REG_ACT_TRANSD_SCALE] << 16 | buff[addr - REG_ACT_TRANSD_SCALE + 1]);
        }
        for (uint8_t addr = REG_ACT_TRANSD_OFFS; addr < REG_ACT_PGA_GAIN; addr += 2)
        {
            uint8_t ch = (addr - REG_ACT_TRANSD_OFFS) / 2;
            _transd_offsets[ch] = std::bit_cast<float>(buff[addr - REG_ACT_TRANSD_OFFS] << 16 | buff[addr - REG_ACT_TRANSD_OFFS + 1]);
        }
        memcpy(_pga_gains, &buff[80], sizeof(_pga_gains));
    }
    return err;
}

bool DataLoggerService::link(queue_service::JsonQueue &out_queue)
{
    read_all_config();
    json msg;
    msg["topic"] = MQTT_TOPIC_CONFIG;

    msg["status"] = {
        {"mode", _mode == 0   ? "IDLE"
                 : _mode == 1 ? "RUN"
                 : _mode == 2 ? "CAL"
                              : "UNKNOWN"},
        {"ntp", _ntp ? "enabled" : "disabled"},
        {"autocal", _autocal ? "enabled" : "disabled"}};

    msg["sampling"] = _sampling_period;

    msg["board"] =
        {
            {"uid", _board_uid},
            {"version", _fw_version},
            {"build", _fw_build}};

    msg["adc_scaling"] = {
        {"ch0", _adc_scales[0]},
        {"ch1", _adc_scales[1]},
        {"ch2", _adc_scales[2]},
        {"ch3", _adc_scales[3]},
        {"ch4", _adc_scales[4]},
        {"ch5", _adc_scales[5]},
        {"ch6", _adc_scales[6]},
        {"ch7", _adc_scales[7]}};

    msg["transd_scaling"] = {
        {"ch0", _transd_scales[0]},
        {"ch1", _transd_scales[1]},
        {"ch2", _transd_scales[2]},
        {"ch3", _transd_scales[3]},
        {"ch4", _transd_scales[4]},
        {"ch5", _transd_scales[5]},
        {"ch6", _transd_scales[6]},
        {"ch7", _transd_scales[7]},
        {"ch8", _transd_scales[8]},
        {"ch9", _transd_scales[9]},
        {"ch10", _transd_scales[10]},
        {"ch11", _transd_scales[11]},
        {"ch12", _transd_scales[12]},
        {"ch13", _transd_scales[13]},
        {"ch14", _transd_scales[14]},
        {"ch15", _transd_scales[15]},
    };

    msg["transd_offsets"] = {
        {"ch0", _transd_offsets[0]},
        {"ch1", _transd_offsets[1]},
        {"ch2", _transd_offsets[2]},
        {"ch3", _transd_offsets[3]},
        {"ch4", _transd_offsets[4]},
        {"ch5", _transd_offsets[5]},
        {"ch6", _transd_offsets[6]},
        {"ch7", _transd_offsets[7]},
    };

    msg["pga_gain"] =
        {
            {"ch0", _pga_gains[0]},
            {"ch1", _pga_gains[1]},
            {"ch2", _pga_gains[2]},
            {"ch3", _pga_gains[3]},
            {"ch4", _pga_gains[4]},
            {"ch5", _pga_gains[5]},
            {"ch6", _pga_gains[6]},
            {"ch7", _pga_gains[7]},
        };

    _is_linked = true;
    out_queue.push(msg);
    return _is_linked;
}

bool DataLoggerService::measurement(queue_service::JsonQueue &out_queue)
{
    read_all_channels();
    json msg;
    msg["topic"] = MQTT_TOPIC_VOLTAGE;
    for (uint8_t ch = 0; ch < 16; ++ch)
        msg[fmt::format("ch{}", ch)] = _adc_channels[ch];
    out_queue.push(msg);
    return true;
}

int DataLoggerService::write_float(uint8_t ch, uint16_t addr, float val)
{
    uint32_t bits = std::bit_cast<uint32_t>(val);
    uint16_t high = static_cast<uint16_t>(bits >> 16);
    uint16_t low = static_cast<uint16_t>(bits & 0xFFFF);
    uint16_t buffer[2] = {high, low};
    return modbus_write_registers(addr + ch * 2u, 2, buffer);
}

int DataLoggerService::read_float(uint8_t ch, uint16_t addr, float &dst)
{
    uint16_t buff[2];
    int err = modbus_read_input_registers(addr + ch * 2u, 2, buff);
    if (err == 0)
        dst = std::bit_cast<float>(buff[0] << 16 | buff[1]);
    return err;
}

// // // *********************** END OF FILE ******************************* //
