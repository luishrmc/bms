// src/main.cpp
// BMS Voltage Monitoring Test - Fixed for Lambda Disposer

#include "periodic_task.hpp"
#include "voltage.hpp"
#include "safe_queue.hpp"
#include "batch_pool.hpp"
#include "batch_structures.hpp"

#include <iostream>
#include <iomanip>
#include <csignal>
#include <boost/atomic.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>

// Atomic flag for graceful shutdown via signal handling
boost::atomic<bool> g_running{true};

void signal_handler(int)
{
    std::cout << "\n[Main] Shutdown signal received..." << std::endl;
    g_running = false;
}

// Helper: Print batch details
void print_batch_summary(const bms::VoltageBatch *batch)
{
    std::cout << "[Consumer] Batch | Device: " << static_cast<int>(batch->device_id)
              << " | Seq: " << batch->seq
              << " | TS Valid: " << (batch->ts.valid ? "Yes" : "No");

    if (bms::any(batch->flags))
    {
        std::cout << " | Flags: 0x" << std::hex
                  << static_cast<std::uint32_t>(batch->flags) << std::dec;
    }
    std::cout << std::endl;
}

// Helper: Print voltage statistics
void print_voltage_stats(const bms::VoltageBatch *batch)
{
    if (bms::any(batch->flags & bms::SampleFlags::CommError))
    {
        std::cout << "  [Error] Communication failure - no data" << std::endl;
        return;
    }

    float min_v = batch->voltages[0];
    float max_v = batch->voltages[0];
    float sum_v = 0.0f;
    int valid_count = 0;

    for (float v : batch->voltages)
    {
        if (std::isfinite(v))
        {
            if (v < min_v)
                min_v = v;
            if (v > max_v)
                max_v = v;
            sum_v += v;
            valid_count++;
        }
    }

    if (valid_count > 0)
    {
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "  Voltages: Min=" << min_v << "V, Max=" << max_v
                  << "V, Avg=" << (sum_v / valid_count)
                  << "V, Pack=" << sum_v << "V" << std::endl;

        // Show first 4 cells for detail
        std::cout << "  First 4: ";
        for (size_t i = 0; i < 4 && i < batch->voltages.size(); ++i)
        {
            std::cout << "C" << i << "=" << batch->voltages[i] << "V ";
        }
        std::cout << std::endl;
    }
}

int main()
{
    // 1. Setup Signal Handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << " BMS Voltage Monitoring Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // ========================================================================
    // 2. Instantiate Shared Resources
    // ========================================================================

    std::cout << "[Main] Creating batch pool (capacity: 128)..." << std::endl;
    bms::VoltageBatchPool voltage_pool(128);
    std::cout << "  Preallocated: " << voltage_pool.preallocated() << " batches" << std::endl;

    // Create queue with pool's disposer (explicit template parameter required!)
    std::cout << "[Main] Creating safe queue (capacity: 64)..." << std::endl;

    // Define queue type with explicit Disposer template parameter
    using VoltageQueueType = bms::SafeQueue<
        bms::VoltageBatch,
        decltype(voltage_pool.disposer()) // Lambda type from pool
        >;

    VoltageQueueType voltage_queue(64, voltage_pool.disposer());

    // ========================================================================
    // 3. Configure Acquisition Module
    // ========================================================================

    std::cout << "[Main] Configuring voltage acquisition..." << std::endl;
    bms::VoltageAcquisitionConfig v_cfg;

    // Device 1
    v_cfg.device1.host = "192.168.7.2";
    v_cfg.device1.port = 502;
    v_cfg.device1.unit_id = 1;
    v_cfg.device1.response_timeout_sec = 2;
    v_cfg.device1.connect_retries = 3;
    v_cfg.device1.read_retries = 2;

    // Device 2
    v_cfg.device2.host = "192.168.7.200";
    v_cfg.device2.port = 502;
    v_cfg.device2.unit_id = 2;
    v_cfg.device2.response_timeout_sec = 2;
    v_cfg.device2.connect_retries = 3;
    v_cfg.device2.read_retries = 2;

    // Policies
    v_cfg.push_failed_reads = true; // Push failed reads for observability
    v_cfg.enable_validation = true; // Physical sanity checks

    std::cout << "  Device 1: " << v_cfg.device1.host << ":" << v_cfg.device1.port << std::endl;
    std::cout << "  Device 2: " << v_cfg.device2.host << ":" << v_cfg.device2.port << std::endl;

    // ========================================================================
    // 4. Instantiate Voltage Acquisition
    // ========================================================================

    std::cout << "[Main] Creating voltage acquisition instance..." << std::endl;
    bms::VoltageAcquisition<VoltageQueueType> voltage_producer(
        v_cfg,
        voltage_pool,
        voltage_queue);

    // Initial connection attempt
    std::cout << "[Main] Connecting to MODBUS devices..." << std::endl;
    if (!voltage_producer.connect())
    {
        std::cerr << "  Warning: Initial connection failed. Will retry during reads." << std::endl;
        std::cerr << "  Device 1: " << voltage_producer.device1_status().last_error << std::endl;
        std::cerr << "  Device 2: " << voltage_producer.device2_status().last_error << std::endl;
    }
    else
    {
        std::cout << "  ✓ Both devices connected successfully" << std::endl;
    }

    // ========================================================================
    // 5. Create Producer Task (Sampling at 1.0 Hz)
    // ========================================================================

    std::cout << "[Main] Creating producer task (period: 1000ms)..." << std::endl;
    bms::PeriodicTask voltage_task(
        boost::chrono::milliseconds(1000),
        std::ref(voltage_producer) // std::ref to avoid copying non-copyable object
    );

    // ========================================================================
    // 6. Create Consumer Task (Processing at 2 Hz)
    // ========================================================================

    std::cout << "[Main] Creating consumer task (period: 500ms)..." << std::endl;

    std::uint64_t consumer_processed = 0;
    std::uint64_t consumer_errors = 0;

    auto consumer_work = [&]()
    {
        bms::VoltageBatch *batch = nullptr;

        // Drain all pending samples from queue (non-blocking)
        while (voltage_queue.try_pop(batch))
        {
            consumer_processed++;

            print_batch_summary(batch);

            if (bms::any(batch->flags))
            {
                consumer_errors++;
            }

            if (batch->ts.valid && !bms::any(batch->flags))
            {
                print_voltage_stats(batch);
            }

            // Return batch to pool via queue disposer
            voltage_queue.dispose(batch);
        }
    };

    bms::PeriodicTask consumer_task(
        boost::chrono::milliseconds(500), // Runs 2x faster than producer
        consumer_work);

    // ========================================================================
    // 7. Start Execution
    // ========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << " Starting Tasks - Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================\n"
              << std::endl;

    voltage_task.start();
    consumer_task.start();

    // ========================================================================
    // 8. Main Loop with Periodic Diagnostics
    // ========================================================================

    int counter = 0;
    while (g_running)
    {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(500));

        // Print diagnostics every 10 seconds
        if (++counter % 20 == 0)
        {
            std::cout << "\n[Main] === Diagnostics ===" << std::endl;
            std::cout << "  Producer:" << std::endl;
            std::cout << "    Published: " << voltage_producer.total_published() << std::endl;
            std::cout << "    Dropped:   " << voltage_producer.total_dropped() << std::endl;
            std::cout << "  Consumer:" << std::endl;
            std::cout << "    Processed: " << consumer_processed << std::endl;
            std::cout << "    Errors:    " << consumer_errors << std::endl;
            std::cout << "  Queue:" << std::endl;
            std::cout << "    Size:      " << voltage_queue.approximate_size() << std::endl;
            std::cout << "    Dropped:   " << voltage_queue.dropped_count() << std::endl;
            std::cout << "  Pool:" << std::endl;
            std::cout << "    In use:    " << voltage_pool.in_use_count() << std::endl;
            std::cout << "    In pool:   " << voltage_pool.in_pool() << std::endl;
            std::cout << "  Device 1:" << std::endl;
            std::cout << "    Reads:     " << voltage_producer.device1_status().successful_reads << std::endl;
            std::cout << "    Failures:  " << voltage_producer.device1_status().read_failures << std::endl;
            std::cout << "  Device 2:" << std::endl;
            std::cout << "    Reads:     " << voltage_producer.device2_status().successful_reads << std::endl;
            std::cout << "    Failures:  " << voltage_producer.device2_status().read_failures << std::endl;
        }
    }

    // ========================================================================
    // 9. Shutdown Sequence (Ordered to prevent memory corruption)
    // ========================================================================

    std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

    // Step 1: Stop periodic tasks
    std::cout << "[Main] Stopping periodic tasks..." << std::endl;
    voltage_task.stop();
    consumer_task.stop();

    // Step 2: Wait for threads to complete
    std::cout << "[Main] Joining threads..." << std::endl;
    voltage_task.join();
    consumer_task.join();
    std::cout << "  ✓ All threads stopped" << std::endl;

    // Step 3: Close queue (no more pushes)
    std::cout << "[Main] Closing queue..." << std::endl;
    voltage_queue.close();

    // Step 4: Disconnect devices
    std::cout << "[Main] Disconnecting MODBUS devices..." << std::endl;
    voltage_producer.disconnect();
    std::cout << "  ✓ Devices disconnected" << std::endl;

    // ========================================================================
    // 10. Final Statistics
    // ========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << " Final Statistics" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Producer:" << std::endl;
    std::cout << "  Published:   " << voltage_producer.total_published() << std::endl;
    std::cout << "  Dropped:     " << voltage_producer.total_dropped() << std::endl;
    std::cout << "Consumer:" << std::endl;
    std::cout << "  Processed:   " << consumer_processed << std::endl;
    std::cout << "  Errors:      " << consumer_errors << std::endl;
    std::cout << "Queue:" << std::endl;
    std::cout << "  Total pushed: " << voltage_queue.total_pushed() << std::endl;
    std::cout << "  Total popped: " << voltage_queue.total_popped() << std::endl;
    std::cout << "  Dropped:      " << voltage_queue.dropped_count() << std::endl;
    std::cout << "Pool:" << std::endl;
    std::cout << "  Acquired:    " << voltage_pool.total_acquired() << std::endl;
    std::cout << "  Released:    " << voltage_pool.total_released() << std::endl;
    std::cout << "  In use:      " << voltage_pool.in_use_count() << std::endl;
    std::cout << "  Leaked:      " << voltage_pool.leaked_on_shutdown() << std::endl;

    if (voltage_pool.leaked_on_shutdown() > 0)
    {
        std::cerr << "\nWARNING: Memory leak detected! "
                  << voltage_pool.leaked_on_shutdown()
                  << " batches not returned to pool." << std::endl;
    }

    std::cout << "\n[Main] Clean exit completed." << std::endl;
    return 0;
}
