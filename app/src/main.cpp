/**
 * @file        main.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Source file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#include "periodic_task.hpp"
#include "voltage.hpp"
#include "influxdb.hpp"
#include "safe_queue.hpp"
#include "batch_pool.hpp"
#include "batch_structures.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

#include <boost/atomic.hpp>
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>

#include <iostream>
#include <csignal>
#include <cstdlib>

boost::atomic<bool> g_running{true};

void signal_handler(int)
{
    std::cout << "\n[Main] Shutdown signal received..." << std::endl;
    g_running = false;
}

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
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << "       BMS Stage 1 Data Logger          " << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\n[Main] Creating batch pool..." << std::endl;
    bms::VoltageBatchPool voltage_pool(128);
    std::cout << "  Voltage pool: " << voltage_pool.preallocated() << " batches" << std::endl;

    std::cout << "[Main] Creating voltage queue..." << std::endl;
    using VoltageQueue = bms::SafeQueue<bms::VoltageBatch, bms::VoltageBatchPool::Deleter>;
    VoltageQueue voltage_queue(
        64,
        bms::VoltageBatchPool::Deleter(voltage_pool.disposer()));
    std::cout << "  Voltage queue capacity: 64" << std::endl;

    std::cout << "\n[Main] Configuring voltage acquisition..." << std::endl;
    bms::VoltageAcquisitionConfig v_cfg;

    v_cfg.device1.host = "192.168.7.2";
    v_cfg.device1.port = 502;
    v_cfg.device1.unit_id = 1;
    v_cfg.device1.response_timeout_sec = 0;
    v_cfg.device1.response_timeout_usec = 30000;
    v_cfg.device1.connect_retries = 3;
    v_cfg.device1.read_retries = 2;

    v_cfg.device2.host = "192.168.7.200";
    v_cfg.device2.port = 502;
    v_cfg.device2.unit_id = 2;
    v_cfg.device2.response_timeout_sec = 0;
    v_cfg.device2.response_timeout_usec = 30000;
    v_cfg.device2.connect_retries = 3;
    v_cfg.device2.read_retries = 2;

    // Stage 1: keep communication failures visible in acquisition diagnostics,
    // but only emit DB rows when both device batches are usable in writer.
    v_cfg.push_failed_reads = true;
    v_cfg.enable_validation = false;

    std::cout << "  Device 1: " << v_cfg.device1.host << ":" << v_cfg.device1.port << std::endl;
    std::cout << "  Device 2: " << v_cfg.device2.host << ":" << v_cfg.device2.port << std::endl;

    std::cout << "\n[Main] Configuring InfluxDB..." << std::endl;
    bms::InfluxDBConfig db_cfg;
    db_cfg.base_url = "http://influxdb3:8181";
    db_cfg.database = "battery_data";
    db_cfg.token = get_token();
    db_cfg.connect_timeout = boost::chrono::milliseconds(1500);
    db_cfg.request_timeout = boost::chrono::milliseconds(5000);
    db_cfg.max_lines_per_post = 2048;
    db_cfg.max_bytes_per_post = 512 * 1024;
    db_cfg.max_buffer_age = boost::chrono::milliseconds(250);
    db_cfg.max_retries = 3;
    db_cfg.retry_delay = boost::chrono::milliseconds(100);
    db_cfg.voltage_precision = 6;

    std::cout << "  URL: " << db_cfg.base_url << std::endl;
    std::cout << "  Database: " << db_cfg.database << std::endl;
    std::cout << "  Table: voltage_current" << std::endl;

    std::cout << "\n[Main] Creating acquisition instances..." << std::endl;
    bms::VoltageAcquisition voltage_producer(v_cfg, voltage_pool, voltage_queue);

    try
    {
        bms::InfluxHTTPClient voltage_current_client(db_cfg);

        std::cout << "[Main] Testing InfluxDB write-endpoint reachability..." << std::endl;
        if (!voltage_current_client.ping())
        {
            std::cerr << "  WARNING: InfluxDB write endpoint probe failed at " << db_cfg.base_url << std::endl;
            std::cerr << "  Startup will continue; verify write diagnostics after launch." << std::endl;
        }
        else
        {
            std::cout << "  ✓ InfluxDB write endpoint is reachable" << std::endl;
        }

        bms::VoltageCurrentWriterTask voltage_current_writer(
            db_cfg,
            voltage_current_client,
            voltage_queue);

        std::cout << "\n[Main] Connecting to MODBUS devices..." << std::endl;
        const bool v_connected = voltage_producer.connect();
        if (!v_connected)
        {
            std::cerr << "  WARNING: Voltage devices connection failed" << std::endl;
            std::cerr << "    Device 1: " << voltage_producer.device1_status().last_error << std::endl;
            std::cerr << "    Device 2: " << voltage_producer.device2_status().last_error << std::endl;
        }
        else
        {
            std::cout << "  ✓ Voltage devices connected" << std::endl;
        }

        std::cout << "\n[Main] Creating periodic tasks..." << std::endl;
        bms::PeriodicTask voltage_task(
            boost::chrono::milliseconds(100),
            std::ref(voltage_producer));

        bms::PeriodicTask voltage_current_task(
            boost::chrono::milliseconds(50),
            std::ref(voltage_current_writer));

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Starting Stage 1 Runtime" << std::endl;
        std::cout << "  Press Ctrl+C to stop" << std::endl;
        std::cout << "========================================\n"
                  << std::endl;

        voltage_task.start();
        voltage_current_task.start();

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            if (++counter % 10 == 0)
            {
                std::cout << "\n=== Stage 1 Diagnostics (t=" << counter << "s) ===" << std::endl;

                std::cout << "\nVoltage Acquisition:" << std::endl;
                std::cout << "  Published: " << voltage_producer.total_published() << std::endl;
                std::cout << "  Dropped: " << voltage_producer.total_dropped() << std::endl;
                std::cout << "  Device 1 reads: " << voltage_producer.device1_status().successful_reads
                          << " (failures: " << voltage_producer.device1_status().read_failures << ")" << std::endl;
                std::cout << "  Device 2 reads: " << voltage_producer.device2_status().successful_reads
                          << " (failures: " << voltage_producer.device2_status().read_failures << ")" << std::endl;

                std::cout << "\nVoltage Queue:" << std::endl;
                std::cout << "  Size: " << voltage_queue.approximate_size()
                          << " (peak: " << voltage_queue.peak_size()
                          << ", dropped: " << voltage_queue.dropped_count() << ")" << std::endl;

                std::cout << "\nVoltageCurrent Writer:" << std::endl;
                std::cout << "  HTTP posts: " << voltage_current_writer.total_posts()
                          << " (failures: " << voltage_current_writer.total_post_failures() << ")" << std::endl;
                std::cout << "  Flushes: threshold=" << voltage_current_writer.diagnostics().threshold_flushes.load()
                          << ", timer=" << voltage_current_writer.diagnostics().timer_flushes.load() << std::endl;
                std::cout << "  Rows written: " << voltage_current_writer.diagnostics().rows_written.load() << std::endl;
                std::cout << "  Invalid/failed batches skipped: " << voltage_current_writer.diagnostics().dropped_invalid_batches.load() << std::endl;
                std::cout << "  Write failures: " << voltage_current_writer.diagnostics().write_failures.load() << std::endl;
                if (!voltage_current_writer.last_error().empty())
                {
                    std::cout << "  Last error: " << voltage_current_writer.last_error() << std::endl;
                }

                std::cout << "\nHTTP Client Stats:" << std::endl;
                std::cout << "  Total HTTP posts: " << voltage_current_client.total_posts() << std::endl;
                std::cout << "  HTTP failures: " << voltage_current_client.total_failures() << std::endl;
                std::cout << "  HTTP retries: " << voltage_current_client.total_retries() << std::endl;
                std::cout << "  Last HTTP code: " << voltage_current_client.last_http_code() << std::endl;

                std::cout << "\nMemory Pool:" << std::endl;
                std::cout << "  Voltage in use: " << voltage_pool.in_use_count()
                          << "/" << voltage_pool.preallocated() << std::endl;
            }
        }

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        voltage_task.stop();
        voltage_current_task.stop();

        voltage_task.join();
        voltage_current_task.join();

        voltage_queue.close();
        voltage_producer.disconnect();

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Final Stage 1 Statistics" << std::endl;
        std::cout << "========================================" << std::endl;

        std::cout << "\nVoltage Acquisition:" << std::endl;
        std::cout << "  Total published: " << voltage_producer.total_published() << std::endl;
        std::cout << "  Total dropped: " << voltage_producer.total_dropped() << std::endl;

        std::cout << "\nVoltageCurrent Writer:" << std::endl;
        std::cout << "  Total posts: " << voltage_current_writer.total_posts() << std::endl;
        std::cout << "  HTTP failures: " << voltage_current_writer.total_post_failures() << std::endl;
        std::cout << "  Rows written: " << voltage_current_writer.diagnostics().rows_written.load() << std::endl;
        std::cout << "  Invalid/failed batches skipped: " << voltage_current_writer.diagnostics().dropped_invalid_batches.load() << std::endl;

        std::cout << "\nQueue:" << std::endl;
        std::cout << "  Voltage: pushed=" << voltage_queue.total_pushed()
                  << ", popped=" << voltage_queue.total_popped()
                  << ", peak=" << voltage_queue.peak_size()
                  << ", dropped=" << voltage_queue.dropped_count() << std::endl;

        std::cout << "\nMemory Pool:" << std::endl;
        std::cout << "  Voltage: acquired=" << voltage_pool.total_acquired()
                  << ", released=" << voltage_pool.total_released()
                  << ", leaked=" << voltage_pool.leaked_on_shutdown() << std::endl;

        if (voltage_pool.leaked_on_shutdown() > 0)
        {
            std::cerr << "\nWARNING: Memory leaks detected!" << std::endl;
        }

        std::cout << "\n[Main] Clean exit completed." << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}
