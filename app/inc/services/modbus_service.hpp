/**
 * @file        modbus_service.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Modbus-TCP Service Header.
 * @version     0.0.1
 * @date        2025-07-21
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
#include <future>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <stop_token>
#include "modbus.hpp"

// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

class ModBusService
{

public:
    enum class reg_type_t : uint8_t
    {
        INPUT = 0,
        HOLDING = 1
    };

    ModBusService(const std::string &ip, uint16_t port, uint8_t s_id);

    std::future<bool> connect(uint8_t max_attempts);
    std::future<bool> disconnect();

    std::future<bool> read_u16(uint16_t addr, uint16_t &dst, reg_type_t reg_t = reg_type_t::INPUT);
    std::future<bool> read_u32(uint16_t addr, uint32_t &dst, reg_type_t reg_t = reg_type_t::INPUT);
    std::future<bool> read_f32(uint16_t addr, float &dst, reg_type_t reg_t = reg_type_t::INPUT);

    std::future<bool> write_u16(uint16_t addr, uint16_t &src);
    std::future<bool> write_u32(uint16_t addr, uint32_t &src);
    std::future<bool> write_f32(uint16_t addr, float &src);

    std::future<bool> read_raw(uint16_t addr, uint16_t length, uint16_t *dst_buffer, reg_type_t reg_t = reg_type_t::INPUT);
    std::future<bool> write_raw(uint16_t addr, uint16_t length, uint16_t *src_buffer);

    std::atomic_bool _connected{false};
private:
    std::string _ip;
    uint16_t _port;
    uint8_t _s_id;

    std::unique_ptr<modbus> _mb;
    mutable std::mutex _mb_mtx;
};

// *********************** END OF FILE ******************************* //
