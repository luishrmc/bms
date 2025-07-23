/**
 * @file        mqtt_ctrl.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.0.1
 * @date        2025-07-21
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
#include "mqtt_ctrl.hpp"
#include <thread>

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

namespace
{
    std::jthread mqtt_task;
}

void start_mqtt_task(MqttService &mqtt)
{
    mqtt_task = std::jthread(
        [&mqtt](std::stop_token stoken)
        {
            using namespace std::chrono_literals;

            while (!stoken.stop_requested())
            {
                std::this_thread::sleep_for(2s); // 2 second loop
            }
            std::cout << "MQTT task exiting..." << std::endl;
        });
}

// *********************** END OF FILE ******************************* //
