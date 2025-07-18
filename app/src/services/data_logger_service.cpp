/**
 * @file        data_logger_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Implementation.
 * @version     0.0.1
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

// *********************** END OF FILE ******************************* //
