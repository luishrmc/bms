/**
 * @file        data_logger_service.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Header.
 *              A service responsible for getting the voltage measurements
 *              from the L2M Datalogger8 rev. 3 BDJI board via Modbus-TCP.
 *
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

#pragma once

// ----------------------------- Includes ----------------------------- //
#include <string>
#include <cstdint>
#include <optional>
#include <memory>
#include "modbus.hpp"

// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

class DataLoggerService
{
public:
    explicit DataLoggerService(const std::string& ip, uint16_t port, uint8_t s_id);

    bool connect(int max_attempts = 3, int retry_delay_ms = 1000);
    void disconnect();

private:
    std::string _ip;
    uint16_t _port;
    uint8_t _s_id;
    bool _connected;
    std::unique_ptr<modbus> _mb;
};

// *********************** END OF FILE ******************************* //
