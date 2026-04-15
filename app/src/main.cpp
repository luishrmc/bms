/**
 * @file        main.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Source file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#include "periodic_task.hpp"
#include "voltage_current.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <csignal>
#include <cstdlib>
#include <iostream>

boost::atomic<bool> g_running{true};

void signal_handler(int)
{
    std::cout << "\n[Main] Shutdown signal received..." << std::endl;
    g_running = false;
}

int main()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << " BMS Stage 1 Voltage-Current Stabilizer " << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\n[Main] Configuring combined voltage-current acquisition..." << std::endl;
    bms::VoltageCurrentAcquisitionConfig vc_cfg;

    vc_cfg.device1.host = "192.168.7.2";
    vc_cfg.device1.port = 502;
    vc_cfg.device1.unit_id = 1;
    vc_cfg.device1.connect_retries = 3;
    vc_cfg.device1.read_retries = 2;

    vc_cfg.device2.host = "192.168.7.200";
    vc_cfg.device2.port = 502;
    vc_cfg.device2.unit_id = 2;
    vc_cfg.device2.connect_retries = 3;
    vc_cfg.device2.read_retries = 2;

    std::cout << "  Device 1: " << vc_cfg.device1.host << ":" << vc_cfg.device1.port << std::endl;
    std::cout << "  Device 2: " << vc_cfg.device2.host << ":" << vc_cfg.device2.port << std::endl;

    std::cout << "\n[Main] Creating combined acquisition instance..." << std::endl;
    bms::VoltageCurrentAcquisition voltage_current_acquisition(vc_cfg);

    try
    {
        std::cout << "\n[Main] Connecting to MODBUS devices..." << std::endl;
        const bool connected = voltage_current_acquisition.connect();
        if (!connected)
        {
            std::cerr << "  WARNING: One or more devices failed initial connect" << std::endl;
            std::cerr << "    Device 1: " << voltage_current_acquisition.device1_status().last_error << std::endl;
            std::cerr << "    Device 2: " << voltage_current_acquisition.device2_status().last_error << std::endl;
        }
        else
        {
            std::cout << "  ✓ Both voltage devices connected" << std::endl;
        }

        std::cout << "\n[Main] Creating periodic task (100 ms)..." << std::endl;
        bms::PeriodicTask voltage_current_task(
            boost::chrono::milliseconds(50),
            std::ref(voltage_current_acquisition));

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Starting Stage 1 Runtime" << std::endl;
        std::cout << "  (Console-only voltage-current validation)" << std::endl;
        std::cout << "  Press Ctrl+C to stop" << std::endl;
        std::cout << "========================================\n"
                  << std::endl;

        voltage_current_task.start();

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            if (++counter % 10 == 0)
            {
                const auto &diag = voltage_current_acquisition.diagnostics();
                std::cout << "\n=== Stage 1 Diagnostics (t=" << counter << "s) ===" << std::endl;
                std::cout << "  Pair attempts: " << diag.pair_attempts.load() << std::endl;
                std::cout << "  Pair successes: " << diag.pair_successes.load() << std::endl;
                std::cout << "  Pair failures: " << diag.pair_failures.load() << std::endl;
                std::cout << "  Device 1 reads: " << diag.device1_successes.load()
                          << " (failures: " << diag.device1_failures.load() << ")" << std::endl;
                std::cout << "  Device 2 reads: " << diag.device2_successes.load()
                          << " (failures: " << diag.device2_failures.load() << ")" << std::endl;
                std::cout << "  Last cycle duration: " << diag.last_cycle_duration_ms.load() << " ms" << std::endl;
            }
        }

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        voltage_current_task.stop();
        voltage_current_task.join();

        voltage_current_acquisition.disconnect();

        const auto &diag = voltage_current_acquisition.diagnostics();
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Final Stage 1 Statistics" << std::endl;
        std::cout << "========================================" << std::endl;

        std::cout << "  Pair attempts: " << diag.pair_attempts.load() << std::endl;
        std::cout << "  Pair successes: " << diag.pair_successes.load() << std::endl;
        std::cout << "  Pair failures: " << diag.pair_failures.load() << std::endl;
        std::cout << "  Device 1 reads: " << diag.device1_successes.load()
                  << " (failures: " << diag.device1_failures.load() << ")" << std::endl;
        std::cout << "  Device 2 reads: " << diag.device2_successes.load()
                  << " (failures: " << diag.device2_failures.load() << ")" << std::endl;
        std::cout << "\n[Main] Clean exit completed." << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}
