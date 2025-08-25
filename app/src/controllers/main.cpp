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

    InfluxDBService db(
        "influxdb3-core",
        8181,
        "apiv3_n7_oUpwKZ7m2k_Y2qTK3UY3S3Py7CG8n8ZPuNz2zyAfL88Hsuu7Mok8KBG8MxJcjAM9NjPA6X3HKUE7ES5HZTA",
        "voltage");

    SPSCQueue<std::array<float, 16>> influx_queue(1024);
    auto data_logger_task = start_data_logger_task(dl, influx_queue);
    auto influxdb_task = start_influxdb_task(db, influx_queue);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}

// *********************** END OF FILE ******************************* //
