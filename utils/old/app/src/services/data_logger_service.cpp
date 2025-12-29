/**
 * @file        data_logger_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Implementation.
 * @version     0.3.0
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
                _is_linked = !read_all_config();
                return _is_linked;
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

int DataLoggerService::read_all_channels()
{
    uint16_t buff[32] = {0};
    std::lock_guard<std::mutex> lock(_mb_mtx);
    int err = modbus_read_input_registers(REG_CH_BASE, 32, buff);
    if (err == 0)
    {
        memset(&_adc_channels, 0, sizeof(_adc_channels));
        for (uint8_t ch = 0; ch < 16; ch += 2)
        {
            _adc_channels[ch / 2] = std::bit_cast<float>(buff[ch] << 16 | buff[ch + 1]);
        }
    }
    return err;
}

int DataLoggerService::read_all_config()
{
    uint16_t buff[88] = {0};
    read_status();
    read_board_uid();
    read_firmware_version();
    return err;
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
