/**
 * @file        main.cpp
 * @brief       Simplified RS485 runtime: battery snapshot acquisition + MQTT publish.
 */

#include "mqtt_task.hpp"
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
        std::cout << "\n[Main] Shutdown signal received..." << std::endl;
        g_running = false;
    }
} // namespace

int main(void)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << " BMS RS485 + MQTT Runtime " << std::endl;
    std::cout << "========================================" << std::endl;

    using SnapshotQueue = bms::RS485Task::SnapshotQueue;
    SnapshotQueue rs485_mqtt_queue(512);

    bms::RS485Task::Config rs485_task_cfg;

    // Keep this explicit until the current scaling is validated on hardware.
    rs485_task_cfg.rs485.current_scale_a_per_lsb = 0.0F;

    rs485_task_cfg.connect_retry_delay_ms = 1000;
    rs485_task_cfg.poll_interval_ms = 1000;

    bms::RS485Task rs485_task(rs485_task_cfg, rs485_mqtt_queue, g_running);

    bms::MQTTTaskConfig mqtt_task_cfg;
    mqtt_task_cfg.retained = true;

    bms::MQTTTask mqtt_task(mqtt_task_cfg, rs485_mqtt_queue, g_running);

    try
    {
        std::cout << "\n[Main] Creating worker threads..." << std::endl;

        boost::thread rs485_thread(std::ref(rs485_task));
        boost::thread mqtt_thread(std::ref(mqtt_task));

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            if (++counter % 10 == 0)
            {
                const auto &mqtt_diag = mqtt_task.diagnostics();

                std::cout << "\n=== Runtime Diagnostics (t=" << counter << "s) ===" << std::endl;
                std::cout << "  [MQTTTask] published=" << mqtt_diag.published_snapshots
                          << " publish_failures=" << mqtt_diag.publish_failures
                          << " reconnect_attempts=" << mqtt_diag.reconnect_attempts
                          << std::endl;

                if (!mqtt_diag.last_error.empty())
                {
                    std::cout << "    last_error=" << mqtt_diag.last_error << std::endl;
                }

                std::cout << "  [Queues]" << std::endl;
                std::cout << "    rs485_mqtt_queue: size=" << rs485_mqtt_queue.approximate_size()
                          << " peak=" << rs485_mqtt_queue.peak_size()
                          << " dropped=" << rs485_mqtt_queue.dropped_count()
                          << std::endl;
            }
        }

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        rs485_mqtt_queue.close();

        rs485_thread.join();
        mqtt_thread.join();

        const auto &mqtt_diag = mqtt_task.diagnostics();
        std::cout << "\n=== Final Diagnostics ===" << std::endl;
        std::cout << "  [MQTTTask] published=" << mqtt_diag.published_snapshots
                  << " publish_failures=" << mqtt_diag.publish_failures
                  << " reconnect_attempts=" << mqtt_diag.reconnect_attempts
                  << std::endl;

        std::cout << "  [Queues]" << std::endl;
        std::cout << "    rs485_mqtt_queue: size=" << rs485_mqtt_queue.approximate_size()
                  << " peak=" << rs485_mqtt_queue.peak_size()
                  << " dropped=" << rs485_mqtt_queue.dropped_count()
                  << std::endl;

        if (!mqtt_diag.last_error.empty())
        {
            std::cout << "  [MQTTTask] last_error=" << mqtt_diag.last_error << std::endl;
        }

        std::cout << "\n[Main] Clean exit completed." << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        rs485_mqtt_queue.close();
        return 1;
    }
}
