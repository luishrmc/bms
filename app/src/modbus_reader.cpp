/**
 * @file        modbus_reader.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Source file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#include "modbus_reader.hpp"

#include <modbus/modbus.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace bms
{

    /** @brief Casts an opaque context pointer to libmodbus type.
 * @param[in] p Opaque context pointer.
 * @return The same pointer reinterpreted as modbus_t*.
 */
static modbus_t *as_modbus(void *p) noexcept
    {
        return reinterpret_cast<modbus_t *>(p);
    }

    /** @brief Creates a MODBUS/TCP client with a copied configuration.
 * @param[in] cfg Host, unit ID, timeout and retry parameters.
 */
ModbusTcpClient::ModbusTcpClient(ModbusTcpConfig cfg)
        : cfg_(std::move(cfg))
    {
    }

    /** @brief Closes and frees any active MODBUS context. */
ModbusTcpClient::~ModbusTcpClient()
    {
        disconnect();
    }

    /** @brief Establishes a new TCP session to the configured MODBUS device.
 * @return True when the connection is established, otherwise false.
 */
bool ModbusTcpClient::connect()
    {
        disconnect();

        modbus_t *ctx = modbus_new_tcp(cfg_.host.c_str(), cfg_.port);
        if (!ctx)
        {
            update_error_("modbus_new_tcp");
            status_.connect_failures++;
            return false;
        }

        if (modbus_set_slave(ctx, cfg_.unit_id) == -1)
        {
            update_error_("modbus_set_slave");
            modbus_free(ctx);
            status_.connect_failures++;
            return false;
        }

        // Configure timeouts (cast to uint32_t for libmodbus API)
        if (modbus_set_response_timeout(ctx,
                                        static_cast<std::uint32_t>(cfg_.response_timeout_sec),
                                        static_cast<std::uint32_t>(cfg_.response_timeout_usec)) == -1)
        {
            update_error_("modbus_set_response_timeout");
            modbus_free(ctx);
            status_.connect_failures++;
            return false;
        }

        if (modbus_set_byte_timeout(ctx,
                                    static_cast<std::uint32_t>(cfg_.byte_timeout_sec),
                                    static_cast<std::uint32_t>(cfg_.byte_timeout_usec)) == -1)
        {
            update_error_("modbus_set_byte_timeout");
            modbus_free(ctx);
            status_.connect_failures++;
            return false;
        }

        // Connection retry loop
        for (int attempt = 0; attempt < cfg_.connect_retries; ++attempt)
        {
            if (modbus_connect(ctx) == 0)
            {
                ctx_ = ctx;
                connected_ = true;
                return true;
            }
            update_error_("modbus_connect");
        }

        modbus_free(ctx);
        status_.connect_failures++;
        return false;
    }

    /** @brief Closes the current TCP session if one is active. */
void ModbusTcpClient::disconnect()
    {
        if (ctx_)
        {
            modbus_t *ctx = as_modbus(ctx_);
            if (connected_)
            {
                modbus_close(ctx);
            }
            modbus_free(ctx);
            ctx_ = nullptr;
        }
        connected_ = false;
    }

    /** @brief Reports whether a valid MODBUS session is currently open.
 * @return True when the client has a live context and connection flag set.
 */
bool ModbusTcpClient::is_connected() const noexcept
    {
        return connected_ && ctx_ != nullptr;
    }

    /** @brief Reconnects on-demand when a read is attempted while disconnected.
 * @return True if connected after the check, otherwise false.
 */
bool ModbusTcpClient::ensure_connected_()
    {
        if (is_connected())
        {
            return true;
        }
        status_.reconnects++;
        return connect();
    }

    /**
 * @brief Reads a contiguous block of MODBUS input registers.
 * @param[in] addr First register address.
 * @param[in] count Number of 16-bit registers to read.
 * @param[out] dest Output buffer receiving register words.
 * @return True when all requested registers are read, otherwise false.
 */
bool ModbusTcpClient::read_input_registers(
        int addr,
        int count,
        std::uint16_t *dest)
    {
        if (!dest || count <= 0)
        {
            errno = EINVAL;
            update_error_("read_input_registers invalid args");
            status_.read_failures++;
            return false;
        }

        for (int attempt = 0; attempt <= cfg_.read_retries; ++attempt)
        {
            if (!ensure_connected_())
            {
                status_.read_failures++;
                return false;
            }

            modbus_t *ctx = as_modbus(ctx_);
            // const int rc = modbus_read_registers(ctx, addr, count, dest);
            const int rc = modbus_read_input_registers(ctx, addr, count, dest);

            if (rc == count)
            {
                status_.successful_reads++;
                return true;
            }

            // Read failed - flush and force reconnect
            update_error_("modbus_read_registers");
            status_.read_failures++;
            modbus_flush(ctx);
            modbus_close(ctx);
            connected_ = false;
        }

        return false;
    }

    /**
 * @brief Reads the canonical BMS register block used by acquisition tasks.
 * @param[out] out_regs Fixed-size destination array with 35 register words.
 * @return True on successful block read, otherwise false.
 */
bool ModbusTcpClient::read_bms_block(
        std::array<std::uint16_t, kRegisterBlockCount> &out_regs)
    {
        // Cast kRegisterBlockCount to int for libmodbus API
        return read_input_registers(
            kModbusStartAddr,
            static_cast<int>(kRegisterBlockCount),
            out_regs.data());
    }

    /** @brief Updates response timeout configuration and active context settings.
 * @param[in] timeout Maximum wait for MODBUS responses in milliseconds.
 */
void ModbusTcpClient::set_response_timeout(std::chrono::milliseconds timeout)
    {
        cfg_.response_timeout_sec = static_cast<int>(timeout.count() / 1000);
        cfg_.response_timeout_usec = static_cast<int>((timeout.count() % 1000) * 1000);

        if (ctx_)
        {
            modbus_set_response_timeout(as_modbus(ctx_),
                                        static_cast<std::uint32_t>(cfg_.response_timeout_sec),
                                        static_cast<std::uint32_t>(cfg_.response_timeout_usec));
        }
    }

    /** @brief Updates inter-byte timeout configuration and active context settings.
 * @param[in] timeout Maximum gap between received bytes in milliseconds.
 */
void ModbusTcpClient::set_byte_timeout(std::chrono::milliseconds timeout)
    {
        cfg_.byte_timeout_sec = static_cast<int>(timeout.count() / 1000);
        cfg_.byte_timeout_usec = static_cast<int>((timeout.count() % 1000) * 1000);

        if (ctx_)
        {
            modbus_set_byte_timeout(as_modbus(ctx_),
                                    static_cast<std::uint32_t>(cfg_.byte_timeout_sec),
                                    static_cast<std::uint32_t>(cfg_.byte_timeout_usec));
        }
    }

    /**
 * @brief Refreshes status_.last_error from current errno/libmodbus error text.
 * @param[in] prefix Context prefix prepended to the generated error message.
 */
void ModbusTcpClient::update_error_(const char *prefix)
    {
        status_.last_errno = errno;
        const char *libmsg = modbus_strerror(errno);
        if (!libmsg)
        {
            libmsg = std::strerror(errno);
        }

        status_.last_error.clear();
        if (prefix)
        {
            status_.last_error.append(prefix);
            status_.last_error.append(": ");
        }
        status_.last_error.append(libmsg ? libmsg : "unknown");
    }

} // namespace bms
