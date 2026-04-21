#include "db_task.hpp"
#include "influxdb.hpp"
#include "rs485_task.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
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

} // namespace

int main(void)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << " BMS RS485 + DB Runtime " << std::endl;
    std::cout << "========================================" << std::endl;

    using SnapshotQueue = bms::RS485Task::SnapshotQueue;
    SnapshotQueue rs485_db_queue(512);

    bms::RS485Task::Config rs485_task_cfg;

    bms::RS485Task rs485_task(rs485_task_cfg, rs485_db_queue, g_running);

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

    bms::DBTask db_task(
        influx_client,
        rs485_db_queue,
        bms::DBTaskConfig{
            .max_lines_per_post = 64,
            .max_payload_bytes = 64 * 1024,
            .flush_interval = std::chrono::milliseconds(2500)});

    try
    {
        std::cout << "\n[Main] Creating worker threads..." << std::endl;

        boost::thread rs485_thread(std::ref(rs485_task));
        boost::thread db_thread(std::ref(db_task));

        int counter = 0;
        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));

            if (++counter % 10 == 0)
            {
                const auto &db_diag = db_task.diagnostics();

                std::cout << "\n=== Runtime Diagnostics (t=" << counter << "s) ===" << std::endl;
                std::cout << "  [DBTask] snapshot_rows=" << db_diag.snapshot_rows_written
                          << " http_posts=" << db_diag.http_posts
                          << " write_failures=" << db_diag.write_failures
                          << " threshold_flushes=" << db_diag.threshold_flushes
                          << " timer_flushes=" << db_diag.timer_flushes
                          << std::endl;

                if (!db_diag.last_error.empty())
                {
                    std::cout << "    last_error=" << db_diag.last_error << std::endl;
                }

                std::cout << "  [Queues]" << std::endl;
                std::cout << "    rs485_db_queue: size=" << rs485_db_queue.approximate_size()
                          << " peak=" << rs485_db_queue.peak_size()
                          << " dropped=" << rs485_db_queue.dropped_count()
                          << std::endl;
            }
        }

        std::cout << "\n[Main] Initiating shutdown sequence..." << std::endl;

        rs485_db_queue.close();

        rs485_thread.join();
        db_thread.join();

        const auto &db_diag = db_task.diagnostics();
        std::cout << "\n=== Final Diagnostics ===" << std::endl;
        std::cout << "  [DBTask] snapshot_rows=" << db_diag.snapshot_rows_written
                  << " http_posts=" << db_diag.http_posts
                  << " write_failures=" << db_diag.write_failures
                  << " threshold_flushes=" << db_diag.threshold_flushes
                  << " timer_flushes=" << db_diag.timer_flushes
                  << std::endl;
        std::cout << "  [Queues]" << std::endl;
        std::cout << "    rs485_db_queue: size=" << rs485_db_queue.approximate_size()
                  << " peak=" << rs485_db_queue.peak_size()
                  << " dropped=" << rs485_db_queue.dropped_count()
                  << std::endl;

        if (!db_diag.last_error.empty())
        {
            std::cout << "  [DBTask] last_error=" << db_diag.last_error << std::endl;
        }

        std::cout << "\n[Main] Clean exit completed." << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        rs485_db_queue.close();
        return 1;
    }
}