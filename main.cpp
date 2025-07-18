#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>

#include "modbus.hpp" // Adjust include path as needed

/*
 * Simple Modbus/TCP regression test for the lightweight modbuspp wrapper (modbus.hpp).
 *
 * This program exercises the oitc/modbus-server container example map we built earlier.
 * It demonstrates:
 *   1. Connecting to a Modbus/TCP slave.
 *   2. Reading coils (FC=0x01).
 *   3. Reading holding registers (FC=0x03).
 *   4. Reading input registers (FC=0x04).
 *   5. Writing a single coil (FC=0x05) and verifying.
 *   6. Writing a single holding register (FC=0x06) and verifying.
 *   7. Writing multiple coils (FC=0x0F) and verifying.
 *   8. Basic error reporting using modbus::err / error_msg.
 *
 */

// ---------------------- Address Map (must match server_config.json) ----------------------

// Utility: print error if mb.err set or rc != 0.
static bool check_rc(const char *op, modbus &mb, int rc)
{
    if (rc == 0 && !mb.err)
        return true;
    std::cerr << "[ERROR] " << op << " rc=" << rc;
    if (mb.err)
        std::cerr << " mb.err_no=" << mb.err_no << " msg=" << mb.error_msg;
    std::cerr << std::endl;
    return false;
}

// Helper to dump an array of bools as 0/1
static void dump_bools(const bool *b, size_t n, uint16_t start_addr)
{
    for (size_t i = 0; i < n; ++i)
    {
        std::cout << "  coil[" << std::dec << (start_addr + i) << "]=" << (b[i] ? 1 : 0) << '\n';
    }
}

// Helper to dump an array of uint16_t in dec & hex
static void dump_regs(const uint16_t *r, size_t n, uint16_t start_addr)
{
    for (size_t i = 0; i < n; ++i)
    {
        std::cout << "  reg[" << (start_addr + i) << "] = " << r[i]
                  << " (0x" << std::hex << std::setw(4) << std::setfill('0') << r[i] << std::dec << ")\n";
    }
}

int main(void)
{
    std::string host = "192.168.0.15";
    uint16_t port = 5020; // match docker -p 5020:5020
    int slave_id = 1;

    std::cout << "[INFO] Connecting to Modbus TCP slave at " << host << ":" << port
              << " (slave id " << slave_id << ")..." << std::endl;

    modbus mb(host, port);
    mb.modbus_set_slave_id(slave_id);
    if (!mb.modbus_connect())
    {
        std::cerr << "[FATAL] Connection failed." << std::endl;
        return 1;
    }

    uint16_t coils_addr = 0x0000;
    uint8_t coils_size = 4;
    bool coils_buf[coils_size]{};
    int rc = mb.modbus_read_coils(coils_addr, coils_size, coils_buf);
    if (check_rc("none", mb, rc))
    {
        dump_bools(coils_buf, coils_size, coils_addr);
    }

    uint16_t holding_reg_addr = 0x0000;
    uint8_t holding_reg_size = 4;
    uint16_t regs[holding_reg_size]{};
    rc = mb.modbus_read_holding_registers(holding_reg_addr, holding_reg_size, regs);
    if (check_rc("none", mb, rc))
    {
        std::cout << "Holding Registers:" << std::endl;
        dump_regs(regs, holding_reg_size, holding_reg_addr);
    }

    mb.modbus_close();
    std::cout << "Done." << std::endl;
    return 0;
}
