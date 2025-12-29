#include "modbus_reader.hpp"

#include <modbus/modbus.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace bms
{

    static modbus_t *as_modbus(void *p) noexcept
    {
        return reinterpret_cast<modbus_t *>(p);
    }

    ModbusTcpClient::ModbusTcpClient(ModbusTcpConfig cfg)
        : cfg_(std::move(cfg))
    {
    }

    ModbusTcpClient::~ModbusTcpClient()
    {
        disconnect();
    }

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

    bool ModbusTcpClient::is_connected() const noexcept
    {
        return connected_ && ctx_ != nullptr;
    }

    bool ModbusTcpClient::ensure_connected_()
    {
        if (is_connected())
        {
            return true;
        }
        status_.reconnects++;
        return connect();
    }

    bool ModbusTcpClient::read_holding_registers(
        int addr,
        int count,
        std::uint16_t *dest)
    {
        if (!dest || count <= 0)
        {
            errno = EINVAL;
            update_error_("read_holding_registers invalid args");
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

    bool ModbusTcpClient::read_bms_block(
        std::array<std::uint16_t, kRegisterBlockCount> &out_regs)
    {
        // Cast kRegisterBlockCount to int for libmodbus API
        return read_holding_registers(
            kModbusStartAddr,
            static_cast<int>(kRegisterBlockCount),
            out_regs.data());
    }

    SampleFlags ModbusTcpClient::read_voltage_batch(VoltageBatch &batch)
    {
        std::array<std::uint16_t, kRegisterBlockCount> registers;

        if (!read_bms_block(registers))
        {
            batch.flags = SampleFlags::CommError;
            batch.ts.valid = false;
            return SampleFlags::CommError;
        }

        // Type-safe call - array passed by reference
        populate_voltage_batch(batch, registers);
        batch.flags = SampleFlags::None;

        return SampleFlags::None;
    }

    SampleFlags ModbusTcpClient::read_temperature_batch(TemperatureBatch &batch)
    {
        std::array<std::uint16_t, kRegisterBlockCount> registers;

        if (!read_bms_block(registers))
        {
            batch.flags = SampleFlags::CommError;
            batch.ts.valid = false;
            return SampleFlags::CommError;
        }

        // Type-safe call - array passed by reference
        populate_temperature_batch(batch, registers);
        batch.flags = SampleFlags::None;

        return SampleFlags::None;
    }

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
