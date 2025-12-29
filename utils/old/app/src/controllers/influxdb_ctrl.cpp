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

#include "influxdb_ctrl.hpp"
#include "config.hpp"
#include <array>
// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

constexpr size_t BATCH_SIZE = 10;
constexpr auto MAX_LATENCY = std::chrono::milliseconds(200); // flush even if batch not full
// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

std::jthread start_influxdb_task(InfluxDBService &db, MPSCQueue<std::string> &influx_queue)
{
    return std::jthread(
        [&db, &influx_queue](std::stop_token stoken)
        {
            std::vector<std::string> batch;
            batch.reserve(10);
            std::string data;
            db.connect();
            while (!stoken.stop_requested())
            {
                influx_queue.pop(data);
                batch.push_back(data);
                if (batch.size() == BATCH_SIZE)
                {
                    db.insert_batch(batch);
                    batch.clear();
                }
            }
        });
}

// *********************** END OF FILE ******************************* //
