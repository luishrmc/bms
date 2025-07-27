/**
 * @file        data_logger_ctrl.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.0.1
 * @date        2025-07-26
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
#include "data_logger_ctrl.hpp"
#include "config.hpp"

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

using json = nlohmann::json;

std::jthread start_data_logger_task(DataLoggerService &dl, queue_service::JsonQueue &q)
{
    return std::jthread(
        [&dl, &q](std::stop_token stoken)
        {
            json msg = {
                {"topic", MQTT_TOPIC_VOLTAGE},
                {"ch0", 1.15},
                {"ch1", 2.30},
                {"ch2", 3.45},
                {"ch3", 4.60},
                {"ch4", 5.14},
                {"ch5", 6.08},
                {"ch6", 7.42},
            };

            while (!stoken.stop_requested())
            {
                q.push(msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        });
}

// *********************** END OF FILE ******************************* //
