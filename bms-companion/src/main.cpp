#include "battery_snapshot.hpp"
#include "rs485_task.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
boost::atomic<bool> g_running{true};

void signal_handler(int)
{
    std::cout << "\n[Main] Shutdown signal received...\n";
    g_running = false;
}
} // namespace

int main(void)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================\n";
    std::cout << " BMS RS485 Snapshot Runtime\n";
    std::cout << "========================================\n";

    // Minimal CLI/env setup for the first step.
    bms::RS485Task::Config rs485_task_cfg;

    // Keep current scaling configurable because the current unit in the pack documentation
    // is not fully trustworthy yet. Set > 0 when you confirm the proper scaling.
    rs485_task_cfg.rs485.current_scale_a_per_lsb = 0.0F;

    rs485_task_cfg.rs485.response_timeout_sec = 0;
    rs485_task_cfg.rs485.response_timeout_usec = 300000;
    rs485_task_cfg.poll_interval_ms = 1000;
    rs485_task_cfg.connect_retry_delay_ms = 1000;

    using SnapshotQueue = bms::RS485Task::SnapshotQueue;
    SnapshotQueue rs485_db_queue(256);

    bms::RS485Task rs485_task(rs485_task_cfg, rs485_db_queue, g_running);

    boost::thread rs485_thread(std::ref(rs485_task));

    // First consumer: just print to terminal.
    boost::thread printer_thread([&]() {
        bms::BatterySnapshot *snapshot = nullptr;

        while (g_running || rs485_db_queue.approximate_size() > 0)
        {
            if (!rs485_db_queue.wait_for_and_pop(snapshot, std::chrono::milliseconds(500)))
            {
                continue;
            }

            if (snapshot != nullptr)
            {
                snapshot->print(std::cout);
                rs485_db_queue.dispose(snapshot);
                snapshot = nullptr;
            }
        }

        // Drain any remaining items after shutdown.
        while (rs485_db_queue.try_pop(snapshot))
        {
            if (snapshot != nullptr)
            {
                snapshot->print(std::cout);
                rs485_db_queue.dispose(snapshot);
                snapshot = nullptr;
            }
        }

        std::cout << "[Printer] Task stopped.\n";
    });

    int counter = 0;
    while (g_running)
    {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

        if (++counter % 10 == 0)
        {
            std::cout << "\n=== Runtime Diagnostics (t=" << counter << "s) ===\n";
            std::cout << "  rs485_db_queue: size=" << rs485_db_queue.approximate_size()
                      << " peak=" << rs485_db_queue.peak_size()
                      << " dropped=" << rs485_db_queue.dropped_count()
                      << "\n";
        }
    }

    std::cout << "\n[Main] Initiating shutdown sequence...\n";

    rs485_db_queue.close();

    rs485_thread.join();
    printer_thread.join();

    std::cout << "[Main] Clean exit completed.\n";
    return 0;
}