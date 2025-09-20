/**
 * @file        bms_controller.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.0.1
 * @date        YYYY-MM-DD
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
#include <iostream>
#include "config.hpp"
#include <thread>
#include <queue>
#include "data_logger_ctrl.hpp"
#include "influxdb_ctrl.hpp"
#include "spsc_ring_service.hpp"

const char *influx_host = std::getenv("INFLUX_HOST");
const char *influx_db = std::getenv("INFLUX_DB");
const char *influx_token = std::getenv("INFLUX_TOKEN");

std::string host = influx_host ? influx_host : "localhost";
int port = 8181;
std::string token = influx_token ? influx_token : "apiv3_VPiuJuMKt8Nawcrvm97YNBtMFUpY6Cv_460ED1QpkW1QLo-U9fhJJZJ0YQsbxnx3PJ1JF_GogCkddk3uQm-gCQ";
// std::string db_name = influx_db ? influx_db : "voltage";
std::string db_name = "voltage";

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //
int main()
{
    std::cout << "[Main] Starting application: " << project_name << " v" << project_version << std::endl;

    DataLoggerService dl("192.168.0.200", 502, 1);
    InfluxDBService db(host, port, token, db_name);

    SPSCQueue<std::array<float, 8>> influx_queue(1024);
    auto data_logger_task = start_data_logger_task(dl, influx_queue);
    auto influxdb_task = start_influxdb_task(db, influx_queue);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}

// *********************** END OF FILE ******************************* //
