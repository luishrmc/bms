/**
 * @file        main.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Source file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#include "periodic_task.hpp"
#include "temperature_console.hpp"
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
    std::cout << " BMS Stage 2 Console Validation Runtime " << std::endl;
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

    std::cout << "\n[Main] Configuring temperature acquisition..." << std::endl;
    bms::TemperatureConsoleAcquisitionConfig temp_cfg;
    temp_cfg.device.host = "192.168.7.201";
    temp_cfg.device.port = 502;
    temp_cfg.device.unit_id = 3;
    temp_cfg.device.connect_retries = 3;
    temp_cfg.device.read_retries = 2;

    std::cout << "  Temperature device: " << temp_cfg.device.host
              << ":" << temp_cfg.device.port
              << " unit_id=" << temp_cfg.device.unit_id << std::endl;

    bms::TemperatureConsoleAcquisition temperature_acquisition(temp_cfg);

    try
    {
        std::cout << "\n[Main] Connecting to MODBUS devices..." << std::endl;
        const bool vc_connected = voltage_current_acquisition.connect();
        const bool temp_connected = temperature_acquisition.connect();

        if (!vc_connected)
        {
            std::cerr << "  WARNING: One or more voltage/current devices failed initial connect" << std::endl;
            std::cerr << "    Device 1: " << voltage_current_acquisition.device1_status().last_error << std::endl;
            std::cerr << "    Device 2: " << voltage_current_acquisition.device2_status().last_error << std::endl;
        }
        else
        {
            std::cout << "  ✓ Both voltage devices connected" << std::endl;
        }

        if (!temp_connected)
        {
            std::cerr << "  WARNING: Temperature device failed initial connect" << std::endl;
            std::cerr << "    Device T: " << temperature_acquisition.device_status().last_error << std::endl;
        }
        else
        {
            std::cout << "  ✓ Temperature device connected" << std::endl;
        }

        std::cout << "\n[Main] Creating periodic tasks (100 ms + 1000 ms)..." << std::endl;
        bms::PeriodicTask voltage_current_task(
            boost::chrono::milliseconds(100),
            std::ref(voltage_current_acquisition));
        bms::PeriodicTask temperature_task(
            boost::chrono::milliseconds(1000),
            std::ref(temperature_acquisition));

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Starting Stage 2 Runtime" << std::endl;
        std::cout << "  (Console-only voltage-current + temperature validation)" << std::endl;
        std::cout << "  Press Ctrl+C to stop" << std::endl;
        std::cout << "========================================\n"
                  << std::endl;

        voltage_current_task.start();
        temperature_task.start();

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            if (++counter % 10 == 0)
            {
                const auto &vc_diag = voltage_current_acquisition.diagnostics();
                const auto &temp_diag = temperature_acquisition.diagnostics();
                std::cout << "\n=== Stage 2 Diagnostics (t=" << counter << "s) ===" << std::endl;
                std::cout << "  [VoltageCurrent] pair_attempts=" << vc_diag.pair_attempts.load()
                          << " pair_successes=" << vc_diag.pair_successes.load()
                          << " pair_failures=" << vc_diag.pair_failures.load()
                          << " cycle_ms=" << vc_diag.last_cycle_duration_ms.load() << std::endl;
                std::cout << "  [VoltageCurrent] device1_ok=" << vc_diag.device1_successes.load()
                          << " device1_fail=" << vc_diag.device1_failures.load()
                          << " device2_ok=" << vc_diag.device2_successes.load()
                          << " device2_fail=" << vc_diag.device2_failures.load() << std::endl;
                std::cout << "  [Temperature] attempts=" << temp_diag.attempts.load()
                          << " successes=" << temp_diag.successes.load()
                          << " failures=" << temp_diag.failures.load()
                          << " cycle_ms=" << temp_diag.last_cycle_duration_ms.load() << std::endl;
            }
        }

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        voltage_current_task.stop();
        temperature_task.stop();
        voltage_current_task.join();
        temperature_task.join();

        voltage_current_acquisition.disconnect();
        temperature_acquisition.disconnect();

        const auto &vc_diag = voltage_current_acquisition.diagnostics();
        const auto &temp_diag = temperature_acquisition.diagnostics();
        std::cout << "\n========================================" << std::endl;
        std::cout << "  Final Stage 2 Statistics" << std::endl;
        std::cout << "========================================" << std::endl;

        std::cout << "  [VoltageCurrent] pair_attempts=" << vc_diag.pair_attempts.load()
                  << " pair_successes=" << vc_diag.pair_successes.load()
                  << " pair_failures=" << vc_diag.pair_failures.load()
                  << " device1_ok=" << vc_diag.device1_successes.load()
                  << " device1_fail=" << vc_diag.device1_failures.load()
                  << " device2_ok=" << vc_diag.device2_successes.load()
                  << " device2_fail=" << vc_diag.device2_failures.load() << std::endl;
        std::cout << "  [Temperature] attempts=" << temp_diag.attempts.load()
                  << " successes=" << temp_diag.successes.load()
                  << " failures=" << temp_diag.failures.load()
                  << " cycle_ms=" << temp_diag.last_cycle_duration_ms.load() << std::endl;
        std::cout << "\n[Main] Clean exit completed." << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}
