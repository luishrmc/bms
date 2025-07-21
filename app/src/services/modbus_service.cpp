/**
 * @file        modbus_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Modbus-TCP Service Implementation.
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

// ----------------------------- Includes ----------------------------- //
#include "modbus_service.hpp"
#include <bit>

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

ModBusService::ModBusService(const std::string &ip, uint16_t port, uint8_t s_id)
    : _ip(ip), _port(port), _s_id(s_id), _connected(false),
      _mb(std::make_unique<modbus>(ip, port))
{
    _mb->modbus_set_slave_id(s_id);
}

std::future<bool> ModBusService::connect(uint8_t max_attempts)
{
    return std::async(
        std::launch::async,
        [this, max_attempts]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (_connected.load(std::memory_order_acquire))
                return true;

            for (uint8_t attempt = 0; attempt < max_attempts; ++attempt)
            {
                if (_mb->modbus_connect())
                {
                    _connected.store(true, std::memory_order_release);
                    return true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // back-off
                }
            }
            return false;
        });
}

std::future<bool> ModBusService::disconnect()
{
    return std::async(
        std::launch::async,
        [this]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (!_connected.load(std::memory_order_acquire))
                return true;

            _mb->modbus_close();
            _connected.store(false, std::memory_order_release);
            return true;
        });
}

std::future<bool> ModBusService::read_u16(uint16_t addr, uint16_t &dst, reg_type_t reg_t)
{
    return std::async(
        std::launch::async,
        [this, addr, &dst, reg_t]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (!_connected.load(std::memory_order_acquire))
                return false;

            uint16_t tmp{};
            int rc = reg_t == reg_type_t::HOLDING
                         ? _mb->modbus_read_holding_registers(addr, 1, &tmp)
                         : _mb->modbus_read_input_registers(addr, 1, &tmp);
            if (rc == 0)
            {
                dst = tmp;
                return true;
            }
            return false;
        });
}

std::future<bool> ModBusService::read_u32(uint16_t addr, uint32_t &dst, reg_type_t reg_t)
{
    return std::async(
        std::launch::async,
        [this, addr, &dst, reg_t]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (!_connected.load(std::memory_order_acquire))
                return false;

            uint16_t buf[2]{};
            int rc = reg_t == reg_type_t::HOLDING
                         ? _mb->modbus_read_holding_registers(addr, 2, buf)
                         : _mb->modbus_read_input_registers(addr, 2, buf);
            if (rc == 0)
            {
                dst = (uint32_t(buf[0]) << 16) | buf[1]; // big-endian word order
                return true;
            }
            return false;
        });
}

std::future<bool> ModBusService::read_f32(uint16_t addr, float &dst, reg_type_t reg_t)
{
    return std::async(
        std::launch::async,
        [this, addr, &dst, reg_t]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (!_connected.load(std::memory_order_acquire))
                return false;

            uint16_t buf[2]{};
            int rc = reg_t == reg_type_t::HOLDING
                         ? _mb->modbus_read_holding_registers(addr, 2, buf)
                         : _mb->modbus_read_input_registers(addr, 2, buf);
            if (rc == 0)
            {
                uint32_t bits = (uint32_t(buf[0]) << 16) | buf[1];
                dst = std::bit_cast<float>(bits);
                return true;
            }
            return false;
        });
}

std::future<bool> ModBusService::write_u16(uint16_t addr, uint16_t &src)
{
    return std::async(
        std::launch::async,
        [this, addr, src]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);
            if (!_connected.load(std::memory_order_acquire))
                return false;
            return _mb->modbus_write_register(addr, src) == 0;
        });
}

std::future<bool> ModBusService::write_u32(uint16_t addr, uint32_t &src)
{
    return std::async(
        std::launch::async,
        [this, addr, src]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);
            if (!_connected.load(std::memory_order_acquire))
                return false;

            uint16_t buf[2] = {static_cast<uint16_t>(src >> 16),
                               static_cast<uint16_t>(src & 0xFFFF)};
            return _mb->modbus_write_registers(addr, 2, buf) == 0;
        });
}

std::future<bool> ModBusService::write_f32(uint16_t addr, float &src)
{
    return std::async(
        std::launch::async,
        [this, addr, bits = std::bit_cast<uint32_t>(src)]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);
            if (!_connected.load(std::memory_order_acquire))
                return false;

            uint16_t buf[2] = {static_cast<uint16_t>(bits >> 16),
                               static_cast<uint16_t>(bits & 0xFFFF)};
            return _mb->modbus_write_registers(addr, 2, buf) == 0;
        });
}

std::future<bool> ModBusService::read_raw(uint16_t addr, uint16_t length, uint16_t *dst_buffer, reg_type_t reg_t)
{
    return std::async(
        std::launch::async,
        [this, addr, length, dst_buffer, reg_t]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (!_connected.load(std::memory_order_acquire))
                return false;

            // Cast destination buffer to pointer
            int rc = (reg_t == reg_type_t::HOLDING)
                         ? _mb->modbus_read_holding_registers(addr, length, dst_buffer)
                         : _mb->modbus_read_input_registers(addr, length, dst_buffer);

            if (rc == 0)
                return true;

            return false;
        });
}

std::future<bool> ModBusService::write_raw(uint16_t addr, uint16_t length, uint16_t *src_buffer)
{
    return std::async(
        std::launch::async,
        [this, addr, length, src_buffer]() -> bool
        {
            std::scoped_lock lock(_mb_mtx);

            if (!_connected.load(std::memory_order_acquire))
                return false;

            int rc = _mb->modbus_write_registers(addr, length, src_buffer);
            if (rc == 0)
                return true;

            return false;
        });
}

// *********************** END OF FILE ******************************* //
