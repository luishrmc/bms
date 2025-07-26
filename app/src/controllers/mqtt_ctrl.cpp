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
#include "logging_service.hpp"
#include <thread>

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

std::jthread start_mqtt_task(MqttService &mqtt)
{
    return std::jthread(
        [&mqtt](std::stop_token stoken)
        {
            while (!stoken.stop_requested())
            {
                if (mqtt.is_connected())
                {
                    static uint32_t counter = 0;
                    counter++;
                    if (counter % 5 == 0) 
                        mqtt.publish("alive", "{\"status\": \"online\"}", false);
                }
                else if (!mqtt.is_connecting())
                {
                    mqtt.connect();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            std::cout << "MQTT task exiting..." << std::endl;
        });
}

// *********************** END OF FILE ******************************* //
