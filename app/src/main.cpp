/**
 * @file        main.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Source file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

// src/main.cpp
// BMS Data Logger - InfluxDB Integration Test

#include "periodic_task.hpp"
#include "voltage.hpp"
#include "temp.hpp"
#include "influxdb.hpp"
#include "safe_queue.hpp"
#include "batch_pool.hpp"
#include "batch_structures.hpp"
#include "db_consumer.hpp"
#include "normalizer.hpp"
#include "soc.hpp"
#include "soh.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

#include <boost/atomic.hpp>
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>

#include <iostream>
#include <iomanip>
#include <csignal>
#include <sstream>
#include <cstdlib>

// ========================================================================
// Global Shutdown Flag
// ========================================================================

boost::atomic<bool> g_running{true};

/** @brief Handles OS termination signals and requests graceful shutdown.
 * @param[in] signum POSIX signal number (unused).
 */
void signal_handler(int)
{
    std::cout << "\n[Main] Shutdown signal received..." << std::endl;
    g_running = false;
}

// ========================================================================
// Helper Functions
// ========================================================================

/**
 * @brief Formats device timestamp into RFC3339 UTC string with millisecond resolution.
 * @param[in] ts Device timestamp structure to format.
 * @return Formatted timestamp string (e.g., 2026-03-25T12:34:56.789Z).
 */
std::string format_timestamp(const bms::DeviceTimestamp &ts)
{
    auto time_t_val = std::chrono::system_clock::to_time_t(ts.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  ts.timestamp.time_since_epoch()) %
              1000;

    std::tm tm_buf;
    gmtime_r(&time_t_val, &tm_buf);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    std::ostringstream oss;
    oss << buffer << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return oss.str();
}

/**
 * @brief Prints one voltage sample to stdout for debugging.
 * @param[in] batch Pointer to the voltage batch in volts (V).
 */
void print_voltage_sample(const bms::VoltageBatch *batch)
{
    std::cout << "[Voltage] Device " << static_cast<int>(batch->device_id)
              << " | Seq " << batch->seq
              << " | " << format_timestamp(batch->ts);

    if (bms::any(batch->flags))
    {
        std::cout << " | Flags: 0x" << std::hex
                  << static_cast<int>(batch->flags) << std::dec;
    }

    if (!bms::any(batch->flags & bms::SampleFlags::CommError))
    {
        std::cout << " | V: ";
        for (size_t i = 0; i < 8 && i < batch->voltages.size(); ++i)
        {
            std::cout << std::fixed << std::setprecision(3)
                      << batch->voltages[i] << "V ";
        }
    }

    std::cout << std::endl;
}

/**
 * @brief Prints one temperature sample to stdout for debugging.
 * @param[in] batch Pointer to the temperature batch in degrees Celsius (°C).
 */
void print_temperature_sample(const bms::TemperatureBatch *batch)
{
    std::cout << "[Temp] Seq " << batch->seq
              << " | " << format_timestamp(batch->ts);

    if (bms::any(batch->flags))
    {
        std::cout << " | Flags: 0x" << std::hex
                  << static_cast<int>(batch->flags) << std::dec;
    }

    if (!bms::any(batch->flags & bms::SampleFlags::CommError))
    {
        std::cout << " | T: ";
        for (size_t i = 0; i < 4 && i < batch->temperatures.size(); ++i)
        {
            std::cout << std::fixed << std::setprecision(1)
                      << batch->temperatures[i] << "°C ";
        }
    }

    std::cout << std::endl;
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
    catch (const nlohmann::json::exception &e)
    {
        std::cout << "[Main] Error: Failed to parse token JSON file." << std::endl;
    }
    return "";
}

// ========================================================================
// Main
// ========================================================================

/**
 * @brief Runs the BMS logger lifecycle: setup, periodic acquisition, persistence and shutdown.
 * @return Exit code 0 on graceful termination, non-zero on fatal startup/runtime errors.
 */
int main()
{
    // 1. Setup Signal Handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << "          BMS Data Logger               " << std::endl;
    std::cout << "========================================" << std::endl;

    // ========================================================================
    // 2. Create Batch Pools
    // ========================================================================

    std::cout << "\n[Main] Creating batch pools..." << std::endl;
    bms::VoltageBatchPool voltage_pool(128);
    bms::TemperatureBatchPool temperature_pool(128);
    std::cout << "  Voltage pool: " << voltage_pool.preallocated() << " batches" << std::endl;
    std::cout << "  Temperature pool: " << temperature_pool.preallocated() << " batches" << std::endl;

    // ========================================================================
    // 3. Create Queues
    // ========================================================================

    std::cout << "[Main] Creating queues..." << std::endl;

    // Use InfluxDBTask's type aliases
    using VoltageQueue = bms::InfluxDBTask::VoltageQueue;
    using TemperatureQueue = bms::InfluxDBTask::TemperatureQueue;

    // Explicitly cast lambda to Deleter type
    VoltageQueue voltage_queue(
        64,
        bms::VoltageBatchPool::Deleter(voltage_pool.disposer()));

    TemperatureQueue temperature_queue(
        64,
        bms::TemperatureBatchPool::Deleter(temperature_pool.disposer()));

    VoltageQueue voltage_normalizer_queue(
        64,
        bms::VoltageBatchPool::Deleter(voltage_pool.disposer()));

    TemperatureQueue temperature_normalizer_queue(
        64,
        bms::TemperatureBatchPool::Deleter(temperature_pool.disposer()));

    std::cout << "  Voltage queue capacity: 64" << std::endl;
    std::cout << "  Temperature queue capacity: 64" << std::endl;
    std::cout << "  Voltage->Normalizer queue capacity: 64" << std::endl;
    std::cout << "  Temperature->Normalizer queue capacity: 64" << std::endl;

    using RowQueue = bms::DBConsumerTask::RowQueue;
    RowQueue soc_queue(512);
    RowQueue soh_queue(512);
    RowQueue normalized_persistence_queue(512);
    std::cout << "  SoC queue capacity: 512" << std::endl;
    std::cout << "  SoH queue capacity: 512" << std::endl;
    std::cout << "  Normalized persistence queue capacity: 512" << std::endl;

    // ========================================================================
    // 4. Configure Voltage Acquisition
    // ========================================================================

    std::cout << "\n[Main] Configuring voltage acquisition..." << std::endl;
    bms::VoltageAcquisitionConfig v_cfg;

    // Device 1
    v_cfg.device1.host = "192.168.7.2";
    v_cfg.device1.port = 502;
    v_cfg.device1.unit_id = 1;
    // Bound response timeout to acquisition cadence (voltage task runs at 100 ms).
    v_cfg.device1.response_timeout_sec = 0;
    v_cfg.device1.response_timeout_usec = 30000; // 30 ms
    v_cfg.device1.connect_retries = 3;
    v_cfg.device1.read_retries = 2;

    // Device 2
    v_cfg.device2.host = "192.168.7.200";
    v_cfg.device2.port = 502;
    v_cfg.device2.unit_id = 2;
    v_cfg.device2.response_timeout_sec = 0;
    v_cfg.device2.response_timeout_usec = 30000; // 30 ms
    v_cfg.device2.connect_retries = 3;
    v_cfg.device2.read_retries = 2;

    v_cfg.push_failed_reads = true;
    v_cfg.enable_validation = true;

    std::cout << "  Device 1: " << v_cfg.device1.host << ":" << v_cfg.device1.port << std::endl;
    std::cout << "  Device 2: " << v_cfg.device2.host << ":" << v_cfg.device2.port << std::endl;

    // ========================================================================
    // 5. Configure Temperature Acquisition
    // ========================================================================

    std::cout << "\n[Main] Configuring temperature acquisition..." << std::endl;
    bms::TemperatureAcquisitionConfig t_cfg;

    t_cfg.device.host = "192.168.7.20";
    t_cfg.device.port = 502;
    t_cfg.device.unit_id = 1;
    // Bound response timeout to acquisition cadence (temperature task runs at 1000 ms).
    t_cfg.device.response_timeout_sec = 0;
    t_cfg.device.response_timeout_usec = 250000; // 250 ms
    t_cfg.device.connect_retries = 3;
    t_cfg.device.read_retries = 2;

    t_cfg.push_failed_reads = true;
    t_cfg.enable_validation = true;

    std::cout << "  Device: " << t_cfg.device.host << ":" << t_cfg.device.port << std::endl;

    // ========================================================================
    // 6. Configure InfluxDB
    // ========================================================================

    std::cout << "\n[Main] Configuring InfluxDB..." << std::endl;
    bms::InfluxDBConfig db_cfg;

    db_cfg.base_url = "http://influxdb3:8181";
    db_cfg.database = "battery_data";
    db_cfg.token = get_token();

    db_cfg.voltage1_table = "voltage1";
    db_cfg.voltage2_table = "voltage2";
    db_cfg.temperature_table = "temperature";

    db_cfg.connect_timeout = boost::chrono::milliseconds(1500);
    db_cfg.request_timeout = boost::chrono::milliseconds(5000);

    db_cfg.max_lines_per_post = 2048;
    db_cfg.max_bytes_per_post = 512 * 1024;
    db_cfg.max_buffer_age = boost::chrono::milliseconds(250);

    db_cfg.max_retries = 3;
    db_cfg.retry_delay = boost::chrono::milliseconds(100);

    db_cfg.include_invalid_samples = false; // Drop flagged samples

    db_cfg.voltage_precision = 6;
    db_cfg.temperature_precision = 3;

    std::cout << "  URL: " << db_cfg.base_url << std::endl;
    std::cout << "  Database: " << db_cfg.database << std::endl;
    std::cout << "  Tables: " << db_cfg.voltage1_table << ", "
              << db_cfg.voltage2_table << ", " << db_cfg.temperature_table << std::endl;
    std::cout << "  Batching: " << db_cfg.max_lines_per_post << " lines / "
              << db_cfg.max_bytes_per_post << " bytes / "
              << db_cfg.max_buffer_age.count() << " ms" << std::endl;

    bms::NormalizerConfig normalizer_cfg;
    normalizer_cfg.initial_cursor = 0;
    normalizer_cfg.default_current_a = 0.0F;

    bms::SoCTaskConfig soc_cfg;
    soc_cfg.initial_expected_cursor = normalizer_cfg.initial_cursor + 1;
    bms::SoHTaskConfig soh_cfg;
    soh_cfg.initial_expected_cursor = normalizer_cfg.initial_cursor + 1;

    // ========================================================================
    // 7. Create Acquisition Instances
    // ========================================================================

    std::cout << "\n[Main] Creating acquisition instances..." << std::endl;

    bms::VoltageAcquisition voltage_producer(v_cfg, voltage_pool, voltage_queue, voltage_normalizer_queue);

    bms::TemperatureAcquisition<TemperatureQueue> temperature_producer(
        t_cfg, temperature_pool, temperature_queue, temperature_normalizer_queue);

    // ========================================================================
    // 8. Create InfluxDB Client and Task
    // ========================================================================

    std::cout << "[Main] Creating InfluxDB client..." << std::endl;

    try
    {
        bms::InfluxHTTPClient raw_influx_client(db_cfg);
        bms::InfluxHTTPClient processed_influx_client(db_cfg);

        std::cout << "[Main] Testing InfluxDB write-endpoint reachability..." << std::endl;
        if (!raw_influx_client.ping())
        {
            std::cerr << "  WARNING: InfluxDB write endpoint probe failed at " << db_cfg.base_url << std::endl;
            std::cerr << "  Startup will continue; verify write diagnostics after launch." << std::endl;
        }
        else
        {
            std::cout << "  ✓ InfluxDB write endpoint is reachable" << std::endl;
        }

        std::cout << "[Main] Creating InfluxDB task..." << std::endl;
        bms::InfluxDBTask influx_task(
            db_cfg,
            raw_influx_client,
            voltage_pool,
            temperature_pool,
            voltage_queue,
            temperature_queue);

        bms::NormalizerTask normalizer_task_worker(
            normalizer_cfg,
            voltage_normalizer_queue,
            temperature_normalizer_queue,
            soc_queue,
            soh_queue,
            normalized_persistence_queue);
        bms::ProcessedTelemetryWriterTask processed_telemetry_writer(
            db_cfg,
            processed_influx_client,
            normalized_persistence_queue);
        bms::SoCTask soc_task_worker(soc_cfg, soc_queue);
        bms::SoHTask soh_task_worker(soh_cfg, soh_queue);

        // ========================================================================
        // 9. Connect MODBUS Devices
        // ========================================================================

        std::cout << "\n[Main] Connecting to MODBUS devices..." << std::endl;

        bool v_connected = voltage_producer.connect();
        bool t_connected = temperature_producer.connect();

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

        if (!t_connected)
        {
            std::cerr << "  WARNING: Temperature device connection failed" << std::endl;
            std::cerr << "    Error: " << temperature_producer.device_status().last_error << std::endl;
        }
        else
        {
            std::cout << "  ✓ Temperature device connected" << std::endl;
        }

        // ========================================================================
        // 10. Create Periodic Tasks
        // ========================================================================

        std::cout << "\n[Main] Creating periodic tasks..." << std::endl;

        bms::PeriodicTask voltage_task(
            boost::chrono::milliseconds(100), // 10 Hz
            std::ref(voltage_producer));

        bms::PeriodicTask temperature_task(
            boost::chrono::milliseconds(1000), // 1 Hz
            std::ref(temperature_producer));

        bms::PeriodicTask influxdb_task(
            boost::chrono::milliseconds(50), // 20 Hz
            std::ref(influx_task));

        bms::PeriodicTask normalizer_task(
            boost::chrono::milliseconds(20),
            std::ref(normalizer_task_worker));

        bms::PeriodicTask processed_telemetry_task(
            boost::chrono::milliseconds(50),
            std::ref(processed_telemetry_writer));

        bms::PeriodicTask soc_task(
            boost::chrono::milliseconds(20),
            std::ref(soc_task_worker));

        bms::PeriodicTask soh_task(
            boost::chrono::milliseconds(20),
            std::ref(soh_task_worker));

        // ========================================================================
        // 11. Optional: Console Monitor (samples every 5s for visibility)
        // ========================================================================

        std::cout << "[Main] Creating console monitor..." << std::endl;

        boost::atomic<std::uint64_t> monitor_voltage{0};
        boost::atomic<std::uint64_t> monitor_temperature{0};

        auto monitor_work = [&]()
        {
            // Optional: Peek at latest samples for console logging
            // For now, just periodic diagnostics
        };

        bms::PeriodicTask monitor_task(
            boost::chrono::milliseconds(5000), // Every 5s
            monitor_work);

        // ========================================================================
        // 12. Start All Tasks
        // ========================================================================

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Starting Data Logger" << std::endl;
        std::cout << "  Press Ctrl+C to stop" << std::endl;
        std::cout << "========================================\n"
                  << std::endl;

        voltage_task.start();
        temperature_task.start();
        influxdb_task.start();
        normalizer_task.start();
        processed_telemetry_task.start();
        soc_task.start();
        soh_task.start();
        monitor_task.start();

        // ========================================================================
        // 13. Main Loop with Diagnostics
        // ========================================================================

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            // Print diagnostics every 10 seconds
            if (++counter % 10 == 0)
            {
                std::cout << "\n=== Diagnostics (t=" << counter << "s) ===" << std::endl;

                std::cout << "\nVoltage Acquisition:" << std::endl;
                std::cout << "  Published: " << voltage_producer.total_published() << std::endl;
                std::cout << "  Dropped: " << voltage_producer.total_dropped() << std::endl;
                std::cout << "  Secondary published: " << voltage_producer.secondary_total_published() << std::endl;
                std::cout << "  Secondary dropped: " << voltage_producer.secondary_total_dropped() << std::endl;
                std::cout << "  Device 1 reads: " << voltage_producer.device1_status().successful_reads
                          << " (failures: " << voltage_producer.device1_status().read_failures << ")" << std::endl;
                std::cout << "  Device 2 reads: " << voltage_producer.device2_status().successful_reads
                          << " (failures: " << voltage_producer.device2_status().read_failures << ")" << std::endl;

                std::cout << "\nTemperature Acquisition:" << std::endl;
                std::cout << "  Published: " << temperature_producer.total_published() << std::endl;
                std::cout << "  Dropped: " << temperature_producer.total_dropped() << std::endl;
                std::cout << "  Secondary published: " << temperature_producer.secondary_total_published() << std::endl;
                std::cout << "  Secondary dropped: " << temperature_producer.secondary_total_dropped() << std::endl;
                std::cout << "  Reads: " << temperature_producer.device_status().successful_reads
                          << " (failures: " << temperature_producer.device_status().read_failures << ")" << std::endl;

                std::cout << "\nInfluxDB Writer:" << std::endl;
                std::cout << "  HTTP posts: " << influx_task.total_posts()
                          << " (failures: " << influx_task.total_post_failures() << ")" << std::endl;
                std::cout << "  Flushes: threshold=" << influx_task.threshold_flushes()
                          << ", timer=" << influx_task.timer_flushes() << std::endl;
                std::cout << "  Voltage samples written: " << influx_task.total_voltage_samples() << std::endl;
                std::cout << "  Temperature samples written: " << influx_task.total_temperature_samples() << std::endl;
                std::cout << "  Dropped (flagged): " << influx_task.dropped_flagged_samples() << std::endl;

                if (!influx_task.last_error().empty())
                {
                    std::cout << "  Last error: " << influx_task.last_error() << std::endl;
                }

                std::cout << "\nQueues:" << std::endl;
                std::cout << "  Voltage queue size: " << voltage_queue.approximate_size()
                          << " (peak: " << voltage_queue.peak_size()
                          << ", dropped: " << voltage_queue.dropped_count() << ")" << std::endl;
                std::cout << "  Temperature queue size: " << temperature_queue.approximate_size()
                          << " (peak: " << temperature_queue.peak_size()
                          << ", dropped: " << temperature_queue.dropped_count() << ")" << std::endl;
                std::cout << "  Voltage->Normalizer queue size: " << voltage_normalizer_queue.approximate_size()
                          << " (peak: " << voltage_normalizer_queue.peak_size()
                          << ", dropped: " << voltage_normalizer_queue.dropped_count() << ")" << std::endl;
                std::cout << "  Temperature->Normalizer queue size: " << temperature_normalizer_queue.approximate_size()
                          << " (peak: " << temperature_normalizer_queue.peak_size()
                          << ", dropped: " << temperature_normalizer_queue.dropped_count() << ")" << std::endl;
                std::cout << "  SoC queue size: " << soc_queue.approximate_size()
                          << " (peak: " << soc_queue.peak_size()
                          << ", dropped: " << soc_queue.dropped_count() << ")" << std::endl;
                std::cout << "  SoH queue size: " << soh_queue.approximate_size()
                          << " (peak: " << soh_queue.peak_size()
                          << ", dropped: " << soh_queue.dropped_count() << ")" << std::endl;
                std::cout << "  Normalized persistence queue size: " << normalized_persistence_queue.approximate_size()
                          << " (peak: " << normalized_persistence_queue.peak_size()
                          << ", dropped: " << normalized_persistence_queue.dropped_count() << ")" << std::endl;

                std::cout << "\nNormalizer:" << std::endl;
                std::cout << "  Voltage batches consumed: " << normalizer_task_worker.diagnostics().voltage_batches_consumed.load() << std::endl;
                std::cout << "  Temperature batches consumed: " << normalizer_task_worker.diagnostics().temperature_batches_consumed.load() << std::endl;
                std::cout << "  Rows published: " << normalizer_task_worker.diagnostics().rows_published.load() << std::endl;
                std::cout << "  Publish failures: " << normalizer_task_worker.diagnostics().publish_failures.load() << std::endl;
                std::cout << "  Rows without temperature: " << normalizer_task_worker.diagnostics().rows_without_temperature.load() << std::endl;
                std::cout << "  Invalid-source rows: " << normalizer_task_worker.diagnostics().invalid_source_rows.load() << std::endl;
                std::cout << "  Last cursor: " << normalizer_task_worker.diagnostics().last_published_cursor.load() << std::endl;
                std::cout << "  Last latency (ms): " << normalizer_task_worker.diagnostics().last_latency_ms.load() << std::endl;
                std::cout << "\nProcessed Telemetry Writer:" << std::endl;
                std::cout << "  HTTP posts: " << processed_telemetry_writer.total_posts()
                          << " (failures: " << processed_telemetry_writer.total_post_failures() << ")" << std::endl;
                std::cout << "  Flushes: threshold=" << processed_telemetry_writer.diagnostics().threshold_flushes.load()
                          << ", timer=" << processed_telemetry_writer.diagnostics().timer_flushes.load() << std::endl;
                std::cout << "  Rows written: " << processed_telemetry_writer.diagnostics().rows_written.load() << std::endl;
                std::cout << "  Write failures: " << processed_telemetry_writer.diagnostics().write_failures.load() << std::endl;
                if (!processed_telemetry_writer.last_error().empty())
                {
                    std::cout << "  Last error: " << processed_telemetry_writer.last_error() << std::endl;
                }

                std::cout << "\nSoC Task:" << std::endl;
                std::cout << "  Processed rows: " << soc_task_worker.diagnostics().rows_processed.load() << std::endl;
                std::cout << "  Duplicates skipped: " << soc_task_worker.diagnostics().duplicates_skipped.load() << std::endl;
                std::cout << "  Out-of-order rows: " << soc_task_worker.diagnostics().out_of_order_rows.load() << std::endl;
                std::cout << "  Processing failures: " << soc_task_worker.diagnostics().processing_failures.load() << std::endl;
                std::cout << "  Last cursor: " << soc_task_worker.diagnostics().last_processed_cursor.load() << std::endl;
                std::cout << "  Last latency (ms): " << soc_task_worker.diagnostics().last_latency_ms.load() << std::endl;

                std::cout << "\nSoH Task:" << std::endl;
                std::cout << "  Processed rows: " << soh_task_worker.diagnostics().rows_processed.load() << std::endl;
                std::cout << "  Duplicates skipped: " << soh_task_worker.diagnostics().duplicates_skipped.load() << std::endl;
                std::cout << "  Out-of-order rows: " << soh_task_worker.diagnostics().out_of_order_rows.load() << std::endl;
                std::cout << "  Processing failures: " << soh_task_worker.diagnostics().processing_failures.load() << std::endl;
                std::cout << "  Last cursor: " << soh_task_worker.diagnostics().last_processed_cursor.load() << std::endl;
                std::cout << "  Last latency (ms): " << soh_task_worker.diagnostics().last_latency_ms.load() << std::endl;

                std::cout << "\nMemory Pools:" << std::endl;
                std::cout << "  Voltage in use: " << voltage_pool.in_use_count()
                          << "/" << voltage_pool.preallocated() << std::endl;
                std::cout << "  Temperature in use: " << temperature_pool.in_use_count()
                          << "/" << temperature_pool.preallocated() << std::endl;

                std::cout << "\nHTTP Client Stats (Raw Writer):" << std::endl;
                std::cout << "  Total HTTP posts: " << raw_influx_client.total_posts() << std::endl;
                std::cout << "  HTTP failures: " << raw_influx_client.total_failures() << std::endl;
                std::cout << "  HTTP retries: " << raw_influx_client.total_retries() << std::endl;
                std::cout << "  Last HTTP code: " << raw_influx_client.last_http_code() << std::endl;
                std::cout << "\nHTTP Client Stats (Processed Writer):" << std::endl;
                std::cout << "  Total HTTP posts: " << processed_influx_client.total_posts() << std::endl;
                std::cout << "  HTTP failures: " << processed_influx_client.total_failures() << std::endl;
                std::cout << "  HTTP retries: " << processed_influx_client.total_retries() << std::endl;
                std::cout << "  Last HTTP code: " << processed_influx_client.last_http_code() << std::endl;

                std::cout << std::endl;
            }
        }

        // ========================================================================
        // 14. Shutdown Sequence
        // ========================================================================

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        std::cout << "[Main] Stopping periodic tasks..." << std::endl;
        voltage_task.stop();
        temperature_task.stop();
        influxdb_task.stop();
        normalizer_task.stop();
        processed_telemetry_task.stop();
        soc_task.stop();
        soh_task.stop();
        monitor_task.stop();

        std::cout << "[Main] Joining threads..." << std::endl;
        voltage_task.join();
        temperature_task.join();
        influxdb_task.join();
        normalizer_task.join();
        processed_telemetry_task.join();
        soc_task.join();
        soh_task.join();
        monitor_task.join();
        std::cout << "  ✓ All threads stopped" << std::endl;

        std::cout << "[Main] Closing queues..." << std::endl;
        voltage_queue.close();
        temperature_queue.close();
        voltage_normalizer_queue.close();
        temperature_normalizer_queue.close();
        soc_queue.close();
        soh_queue.close();
        normalized_persistence_queue.close();

        std::cout << "[Main] Disconnecting MODBUS devices..." << std::endl;
        voltage_producer.disconnect();
        temperature_producer.disconnect();
        std::cout << "  ✓ Devices disconnected" << std::endl;

        // ========================================================================
        // 15. Final Statistics
        // ========================================================================

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Final Statistics" << std::endl;
        std::cout << "========================================" << std::endl;

        std::cout << "\nVoltage Acquisition:" << std::endl;
        std::cout << "  Total published: " << voltage_producer.total_published() << std::endl;
        std::cout << "  Total dropped: " << voltage_producer.total_dropped() << std::endl;

        std::cout << "\nTemperature Acquisition:" << std::endl;
        std::cout << "  Total published: " << temperature_producer.total_published() << std::endl;
        std::cout << "  Total dropped: " << temperature_producer.total_dropped() << std::endl;

        std::cout << "\nInfluxDB Writer:" << std::endl;
        std::cout << "  HTTP posts: " << influx_task.total_posts() << std::endl;
        std::cout << "  HTTP failures: " << influx_task.total_post_failures() << std::endl;
        std::cout << "  Flushes: threshold=" << influx_task.threshold_flushes()
                  << ", timer=" << influx_task.timer_flushes() << std::endl;
        std::cout << "  Voltage samples: " << influx_task.total_voltage_samples() << std::endl;
        std::cout << "  Temperature samples: " << influx_task.total_temperature_samples() << std::endl;
        std::cout << "  Dropped (flagged): " << influx_task.dropped_flagged_samples() << std::endl;

        std::cout << "\nHTTP Client (Raw Writer):" << std::endl;
        std::cout << "  Total posts: " << raw_influx_client.total_posts() << std::endl;
        std::cout << "  Failures: " << raw_influx_client.total_failures() << std::endl;
        std::cout << "  Retries: " << raw_influx_client.total_retries() << std::endl;
        std::cout << "\nHTTP Client (Processed Writer):" << std::endl;
        std::cout << "  Total posts: " << processed_influx_client.total_posts() << std::endl;
        std::cout << "  Failures: " << processed_influx_client.total_failures() << std::endl;
        std::cout << "  Retries: " << processed_influx_client.total_retries() << std::endl;

        std::cout << "\nNormalizer:" << std::endl;
        std::cout << "  Voltage batches consumed: " << normalizer_task_worker.diagnostics().voltage_batches_consumed.load() << std::endl;
        std::cout << "  Temperature batches consumed: " << normalizer_task_worker.diagnostics().temperature_batches_consumed.load() << std::endl;
        std::cout << "  Rows published: " << normalizer_task_worker.diagnostics().rows_published.load() << std::endl;
        std::cout << "  Publish failures: " << normalizer_task_worker.diagnostics().publish_failures.load() << std::endl;
        std::cout << "  Rows without temperature: " << normalizer_task_worker.diagnostics().rows_without_temperature.load() << std::endl;
        std::cout << "  Invalid-source rows: " << normalizer_task_worker.diagnostics().invalid_source_rows.load() << std::endl;
        std::cout << "  Last cursor: " << normalizer_task_worker.diagnostics().last_published_cursor.load() << std::endl;
        std::cout << "  Last latency (ms): " << normalizer_task_worker.diagnostics().last_latency_ms.load() << std::endl;
        std::cout << "\nProcessed Telemetry Writer:" << std::endl;
        std::cout << "  HTTP posts: " << processed_telemetry_writer.total_posts() << std::endl;
        std::cout << "  HTTP failures: " << processed_telemetry_writer.total_post_failures() << std::endl;
        std::cout << "  Flushes: threshold=" << processed_telemetry_writer.diagnostics().threshold_flushes.load()
                  << ", timer=" << processed_telemetry_writer.diagnostics().timer_flushes.load() << std::endl;
        std::cout << "  Rows written: " << processed_telemetry_writer.diagnostics().rows_written.load() << std::endl;
        std::cout << "  Write failures: " << processed_telemetry_writer.diagnostics().write_failures.load() << std::endl;

        std::cout << "\nSoC Task:" << std::endl;
        std::cout << "  Processed rows: " << soc_task_worker.diagnostics().rows_processed.load() << std::endl;
        std::cout << "  Processing failures: " << soc_task_worker.diagnostics().processing_failures.load() << std::endl;
        std::cout << "  Last cursor: " << soc_task_worker.diagnostics().last_processed_cursor.load() << std::endl;
        std::cout << "  Last latency (ms): " << soc_task_worker.diagnostics().last_latency_ms.load() << std::endl;

        std::cout << "\nSoH Task:" << std::endl;
        std::cout << "  Processed rows: " << soh_task_worker.diagnostics().rows_processed.load() << std::endl;
        std::cout << "  Processing failures: " << soh_task_worker.diagnostics().processing_failures.load() << std::endl;
        std::cout << "  Last cursor: " << soh_task_worker.diagnostics().last_processed_cursor.load() << std::endl;
        std::cout << "  Last latency (ms): " << soh_task_worker.diagnostics().last_latency_ms.load() << std::endl;

        std::cout << "\nQueues:" << std::endl;
        std::cout << "  Voltage: pushed=" << voltage_queue.total_pushed()
                  << ", popped=" << voltage_queue.total_popped()
                  << ", peak=" << voltage_queue.peak_size()
                  << ", dropped=" << voltage_queue.dropped_count() << std::endl;
        std::cout << "  Temperature: pushed=" << temperature_queue.total_pushed()
                  << ", popped=" << temperature_queue.total_popped()
                  << ", peak=" << temperature_queue.peak_size()
                  << ", dropped=" << temperature_queue.dropped_count() << std::endl;
        std::cout << "  Voltage->Normalizer: pushed=" << voltage_normalizer_queue.total_pushed()
                  << ", popped=" << voltage_normalizer_queue.total_popped()
                  << ", peak=" << voltage_normalizer_queue.peak_size()
                  << ", dropped=" << voltage_normalizer_queue.dropped_count() << std::endl;
        std::cout << "  Temperature->Normalizer: pushed=" << temperature_normalizer_queue.total_pushed()
                  << ", popped=" << temperature_normalizer_queue.total_popped()
                  << ", peak=" << temperature_normalizer_queue.peak_size()
                  << ", dropped=" << temperature_normalizer_queue.dropped_count() << std::endl;
        std::cout << "  SoC: pushed=" << soc_queue.total_pushed()
                  << ", popped=" << soc_queue.total_popped()
                  << ", peak=" << soc_queue.peak_size()
                  << ", dropped=" << soc_queue.dropped_count() << std::endl;
        std::cout << "  SoH: pushed=" << soh_queue.total_pushed()
                  << ", popped=" << soh_queue.total_popped()
                  << ", peak=" << soh_queue.peak_size()
                  << ", dropped=" << soh_queue.dropped_count() << std::endl;
        std::cout << "  NormalizedPersistence: pushed=" << normalized_persistence_queue.total_pushed()
                  << ", popped=" << normalized_persistence_queue.total_popped()
                  << ", peak=" << normalized_persistence_queue.peak_size()
                  << ", dropped=" << normalized_persistence_queue.dropped_count() << std::endl;

        std::cout << "\nMemory Pools:" << std::endl;
        std::cout << "  Voltage: acquired=" << voltage_pool.total_acquired()
                  << ", released=" << voltage_pool.total_released()
                  << ", leaked=" << voltage_pool.leaked_on_shutdown() << std::endl;
        std::cout << "  Temperature: acquired=" << temperature_pool.total_acquired()
                  << ", released=" << temperature_pool.total_released()
                  << ", leaked=" << temperature_pool.leaked_on_shutdown() << std::endl;

        if (voltage_pool.leaked_on_shutdown() > 0 || temperature_pool.leaked_on_shutdown() > 0)
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
