/**
 * @file        main.cpp
 * @brief       Simplified operational runtime: measurement + DB publisher + SoC/SoH interfaces.
 */

#include "db_publisher.hpp"
#include "influxdb.hpp"
#include "periodic_task.hpp"
#include "soc.hpp"
#include "soh.hpp"
#include "temperature.hpp"
#include "voltage_current.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

boost::atomic<bool> g_running{true};

/**
 * @brief Handles process termination signals.
 * @param Unused signal number.
 */
void signal_handler(int)
{
    std::cout << "\n[Main] Shutdown signal received..." << std::endl;
    g_running = false;
}

/**
 * @brief Loads InfluxDB authentication token from config/influxdb3/token.json.
 * @return Token string, or an empty string if unavailable/invalid.
 */
std::string get_token()
{
    try
    {
        nlohmann::json j;
        std::ifstream token_file("config/influxdb3/token.json");
        token_file >> j;
        token_file.close();
        if (j.contains("token") && j["token"].is_string())
        {
            return j["token"].get<std::string>();
        }
    }
    catch (const nlohmann::json::exception &)
    {
        std::cout << "[Main] Error: Failed to parse token JSON file." << std::endl;
    }
    return "";
}

int main()
{
    // Install signal hooks first so every later phase can shutdown cooperatively.
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << " BMS Simplified Operational Runtime " << std::endl;
    std::cout << "========================================" << std::endl;

    // Create one queue pair per downstream consumer to keep processing paths decoupled.
    bms::DBPublisherTask::VoltageQueue db_voltage_queue(2048);
    bms::DBPublisherTask::TemperatureQueue db_temperature_queue(512);
    bms::SoCTask::VoltageQueue soc_voltage_queue(2048);
    bms::SoCTask::TemperatureQueue soc_temperature_queue(512);
    bms::SoHTask::VoltageQueue soh_voltage_queue(2048);
    bms::SoHTask::TemperatureQueue soh_temperature_queue(512);

    // Fan out each voltage/current sample into independent queue ownership domains.
    auto publish_voltage_sample = [&](const bms::VoltageCurrentSample &sample) {
        auto *db_copy = new bms::VoltageCurrentSample(sample);
        if (!db_voltage_queue.push_blocking(db_copy))
        {
            db_voltage_queue.dispose(db_copy);
        }

        auto *soc_copy = new bms::VoltageCurrentSample(sample);
        if (!soc_voltage_queue.push_blocking(soc_copy))
        {
            soc_voltage_queue.dispose(soc_copy);
        }

        auto *soh_copy = new bms::VoltageCurrentSample(sample);
        if (!soh_voltage_queue.push_blocking(soh_copy))
        {
            soh_voltage_queue.dispose(soh_copy);
        }
    };

    // Fan out each temperature sample into independent queue ownership domains.
    auto publish_temperature_sample = [&](const bms::TemperatureSample &sample) {
        auto *db_copy = new bms::TemperatureSample(sample);
        if (!db_temperature_queue.push_blocking(db_copy))
        {
            db_temperature_queue.dispose(db_copy);
        }

        auto *soc_copy = new bms::TemperatureSample(sample);
        if (!soc_temperature_queue.push_blocking(soc_copy))
        {
            soc_temperature_queue.dispose(soc_copy);
        }

        auto *soh_copy = new bms::TemperatureSample(sample);
        if (!soh_temperature_queue.push_blocking(soh_copy))
        {
            soh_temperature_queue.dispose(soh_copy);
        }
    };

    // Build acquisition task configurations for the two MODBUS acquisition paths.
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
    voltage_current_acquisition.set_sample_callback(publish_voltage_sample);

    bms::TemperatureAcquisitionConfig temp_cfg;
    temp_cfg.device.host = "192.168.7.20";
    temp_cfg.device.port = 502;
    temp_cfg.device.unit_id = 3;
    temp_cfg.device.connect_retries = 3;
    temp_cfg.device.read_retries = 2;

    bms::TemperatureAcquisition temperature_acquisition(temp_cfg);
    temperature_acquisition.set_sample_callback(publish_temperature_sample);

    // Resolve InfluxDB credentials from environment first, then JSON token file.
    bms::InfluxDBConfig influx_cfg;
    if (const char *token = std::getenv("INFLUXDB3_TOKEN"))
    {
        influx_cfg.token = token;
    }
    else
    {
        influx_cfg.token = get_token();
    }

    bms::InfluxHTTPClient influx_client(influx_cfg);
    bms::DBPublisherTask db_publisher(
        influx_client,
        db_voltage_queue,
        db_temperature_queue,
        bms::DBPublisherConfig{.max_lines_per_post = 256,
                               .max_payload_bytes = 128 * 1024,
                               .flush_interval = std::chrono::milliseconds(200)});

    bms::SoCTask soc_task(bms::SoCTaskConfig{}, soc_voltage_queue, soc_temperature_queue);
    bms::SoHTask soh_task(bms::SoHTaskConfig{}, soh_voltage_queue, soh_temperature_queue);

    try
    {
        // Establish initial connectivity before worker threads start.
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

        // Start periodic producers and long-running consumer threads.
        std::cout << "\n[Main] Creating tasks and worker threads..." << std::endl;
        bms::PeriodicTask voltage_current_task(boost::chrono::milliseconds(100), std::ref(voltage_current_acquisition));
        bms::PeriodicTask temperature_task(boost::chrono::milliseconds(1000), std::ref(temperature_acquisition));

        boost::thread db_publisher_thread(std::ref(db_publisher));
        boost::thread soc_thread(std::ref(soc_task));
        boost::thread soh_thread(std::ref(soh_task));

        voltage_current_task.start();
        temperature_task.start();

        // Emit periodic operational diagnostics while the runtime remains active.
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
                          << " http_posts=" << db_diag.http_posts
                          << " write_failures=" << db_diag.write_failures << std::endl;
                std::cout << "    db_q(vc): size=" << db_voltage_queue.approximate_size()
                          << " peak=" << db_voltage_queue.peak_size()
                          << " dropped=" << db_voltage_queue.dropped_count() << std::endl;
                std::cout << "    db_q(temp): size=" << db_temperature_queue.approximate_size()
                          << " peak=" << db_temperature_queue.peak_size()
                          << " dropped=" << db_temperature_queue.dropped_count() << std::endl;
                std::cout << "  [SoC] frames_with_both=" << soc_diag.frames_with_both_measurements
                          << " last_vc_seq=" << soc_diag.last_voltage_sequence
                          << " last_temp_seq=" << soc_diag.last_temperature_sequence << std::endl;
                std::cout << "    soc_q(vc): size=" << soc_voltage_queue.approximate_size()
                          << " peak=" << soc_voltage_queue.peak_size()
                          << " dropped=" << soc_voltage_queue.dropped_count() << std::endl;
                std::cout << "    soc_q(temp): size=" << soc_temperature_queue.approximate_size()
                          << " peak=" << soc_temperature_queue.peak_size()
                          << " dropped=" << soc_temperature_queue.dropped_count() << std::endl;
                std::cout << "  [SoH] frames_with_both=" << soh_diag.frames_with_both_measurements
                          << " last_vc_seq=" << soh_diag.last_voltage_sequence
                          << " last_temp_seq=" << soh_diag.last_temperature_sequence << std::endl;
                std::cout << "    soh_q(vc): size=" << soh_voltage_queue.approximate_size()
                          << " peak=" << soh_voltage_queue.peak_size()
                          << " dropped=" << soh_voltage_queue.dropped_count() << std::endl;
                std::cout << "    soh_q(temp): size=" << soh_temperature_queue.approximate_size()
                          << " peak=" << soh_temperature_queue.peak_size()
                          << " dropped=" << soh_temperature_queue.dropped_count() << std::endl;
            }
        }

        // Shutdown sequence: stop producers, close queues, join workers, then disconnect IO.
        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        voltage_current_task.stop();
        temperature_task.stop();

        db_voltage_queue.close();
        db_temperature_queue.close();
        soc_voltage_queue.close();
        soc_temperature_queue.close();
        soh_voltage_queue.close();
        soh_temperature_queue.close();

        voltage_current_task.join();
        temperature_task.join();

        db_publisher_thread.join();
        soc_thread.join();
        soh_thread.join();

        voltage_current_acquisition.disconnect();
        temperature_acquisition.disconnect();

        std::cout << "\n[Main] Clean exit completed." << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        db_voltage_queue.close();
        db_temperature_queue.close();
        soc_voltage_queue.close();
        soc_temperature_queue.close();
        soh_voltage_queue.close();
        soh_temperature_queue.close();
        return 1;
    }
}
