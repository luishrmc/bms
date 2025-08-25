/**
 * @file        influxdb_ctrl.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.0.1
 * @date        2025-08-24
 *               _   _  _____  __  __   _____
 *              | | | ||  ___||  \/  | / ____|
 *              | | | || |_   | \  / || |  __
 *              | | | ||  _|  | |\/| || | |_ |
 *              | |_| || |    | |  | || |__| |
 *               \___/ |_|    |_|  |_| \_____|
 *
 *            Universidade Federal de Minas Gerais
 *                DELT · BMS Project
 */

// ----------------------------- Includes ----------------------------- //

#include "spsc_ring_service.hpp"
#include "influxdb_ctrl.hpp"
#include "config.hpp"
#include "logging_service.hpp"
#include <iomanip>
// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

constexpr size_t BATCH_SIZE = 10;
constexpr auto MAX_LATENCY = std::chrono::milliseconds(200); // flush even if batch not full
// ---------------------- Function Prototypes -------------------------- //

void data_to_lp(std::array<float, 16> &data, std::vector<std::string> &batch);
// ------------------------- Main Functions ---------------------------- //

std::jthread start_influxdb_task(InfluxDBService &db, SPSCQueue<std::array<float, 16>> &influx_queue)
{
    return std::jthread(
        [&db, &influx_queue](std::stop_token stoken)
        {
            std::vector<std::string> batch;
            batch.reserve(12);
            std::array<float, 16> data;
            db.connect();
            auto last_flush = std::chrono::steady_clock::now();
            while (!stoken.stop_requested())
            {
                if (influx_queue.try_pop(data))
                {
                    data_to_lp(data, batch);
                    if (batch.size() == BATCH_SIZE)
                    {
                        db.insert_batch(batch);
                        app_log(LogLevel::Info, "Flushed batch to InfluxDB");
                        batch.clear();
                        last_flush = std::chrono::steady_clock::now();
                    }
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Time-based flush to bound latency
                if (!batch.empty() &&
                    (std::chrono::steady_clock::now() - last_flush) > MAX_LATENCY)
                {
                    db.insert_batch(batch);
                    batch.clear();
                    last_flush = std::chrono::steady_clock::now();
                }
            }
        });
}

void data_to_lp(std::array<float, 16> &data, std::vector<std::string> &batch)
{
    std::ostringstream lp_line;
    lp_line << "bank0,sensor_id=35786FCF ";
    lp_line << std::fixed << std::setprecision(5);

    for (std::size_t ch = 0; ch < data.size(); ++ch)
    {
        lp_line << "ch" << (ch) << "=" << data[ch];
        if (ch + 1 < data.size())
            lp_line << ",";
    }
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    lp_line << " " << ns;
    batch.push_back(lp_line.str());
}

// *********************** END OF FILE ******************************* //
