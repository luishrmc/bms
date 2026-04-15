/**
 * @file        main.cpp
 * @brief       Simplified operational runtime: measurement + DB publisher + SoC/SoH interfaces.
 */

#include "db_publisher.hpp"
#include "influxdb.hpp"
#include "measurement_bus.hpp"
#include "periodic_task.hpp"
#include "soc.hpp"
#include "soh.hpp"
#include "temperature.hpp"
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
    std::cout << " BMS Simplified Operational Runtime " << std::endl;
    std::cout << "========================================" << std::endl;

    bms::MeasurementBus measurement_bus;

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
    vc_cfg.current_source_channel = 7;
    vc_cfg.current_scale_a_per_v = 1.0F;
    vc_cfg.current_offset_a = 0.0F;

    bms::VoltageCurrentAcquisition voltage_current_acquisition(vc_cfg);
    voltage_current_acquisition.set_sample_callback([&measurement_bus](const bms::VoltageCurrentSample &sample) {
        measurement_bus.publish(sample);
    });

    bms::TemperatureAcquisitionConfig temp_cfg;
    temp_cfg.device.host = "192.168.7.201";
    temp_cfg.device.port = 502;
    temp_cfg.device.unit_id = 3;
    temp_cfg.device.connect_retries = 3;
    temp_cfg.device.read_retries = 2;

    bms::TemperatureAcquisition temperature_acquisition(temp_cfg);
    temperature_acquisition.set_sample_callback([&measurement_bus](const bms::TemperatureSample &sample) {
        measurement_bus.publish(sample);
    });

    bms::InfluxDBConfig influx_cfg;
    if (const char *token = std::getenv("INFLUXDB3_TOKEN"))
    {
        influx_cfg.token = token;
    }

    bms::InfluxHTTPClient influx_client(influx_cfg);
    bms::DBPublisherTask db_publisher(influx_client, measurement_bus);

    bms::SoCTask soc_task(bms::SoCTaskConfig{}, measurement_bus);
    bms::SoHTask soh_task(bms::SoHTaskConfig{}, measurement_bus);

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

        if (!temp_connected)
        {
            std::cerr << "  WARNING: Temperature device failed initial connect" << std::endl;
            std::cerr << "    Device T: " << temperature_acquisition.device_status().last_error << std::endl;
        }

        std::cout << "\n[Main] Creating periodic tasks..." << std::endl;
        bms::PeriodicTask voltage_current_task(boost::chrono::milliseconds(100), std::ref(voltage_current_acquisition));
        bms::PeriodicTask temperature_task(boost::chrono::milliseconds(1000), std::ref(temperature_acquisition));
        bms::PeriodicTask db_publisher_task(boost::chrono::milliseconds(200), std::ref(db_publisher));
        bms::PeriodicTask soc_interface_task(boost::chrono::milliseconds(250), std::ref(soc_task));
        bms::PeriodicTask soh_interface_task(boost::chrono::milliseconds(250), std::ref(soh_task));

        voltage_current_task.start();
        temperature_task.start();
        db_publisher_task.start();
        soc_interface_task.start();
        soh_interface_task.start();

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            if (++counter % 10 == 0)
            {
                const auto &db_diag = db_publisher.diagnostics();
                const auto &soc_diag = soc_task.diagnostics();
                const auto &soh_diag = soh_task.diagnostics();
                std::cout << "\n=== Runtime Diagnostics (t=" << counter << "s) ===" << std::endl;
                std::cout << "  [DBPublisher] voltage_rows=" << db_diag.voltage_rows_written
                          << " temperature_rows=" << db_diag.temperature_rows_written
                          << " write_failures=" << db_diag.write_failures << std::endl;
                std::cout << "  [SoC] frames_with_both=" << soc_diag.frames_with_both_measurements
                          << " last_vc_seq=" << soc_diag.last_voltage_sequence
                          << " last_temp_seq=" << soc_diag.last_temperature_sequence << std::endl;
                std::cout << "  [SoH] frames_with_both=" << soh_diag.frames_with_both_measurements
                          << " last_vc_seq=" << soh_diag.last_voltage_sequence
                          << " last_temp_seq=" << soh_diag.last_temperature_sequence << std::endl;
            }
        }

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        voltage_current_task.stop();
        temperature_task.stop();
        db_publisher_task.stop();
        soc_interface_task.stop();
        soh_interface_task.stop();

        voltage_current_task.join();
        temperature_task.join();
        db_publisher_task.join();
        soc_interface_task.join();
        soh_interface_task.join();

        voltage_current_acquisition.disconnect();
        temperature_acquisition.disconnect();

        std::cout << "\n[Main] Clean exit completed." << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}
