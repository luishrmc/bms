/**
 * @file        mqtt_ctrl.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.1.0
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
#include "nlohmann/json.hpp"

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

using json = nlohmann::json;

std::jthread start_mqtt_task(MqttService &mqtt, queue_service::JsonQueue &q)
{
    return std::jthread(
        [&mqtt, &q](std::stop_token stoken)
        {
            while (!stoken.stop_requested())
            {
                if (mqtt.is_connected())
                {
                    auto msg = q.try_pop();
                    if (msg != std::nullopt)
                        mqtt.publish("bms/ufmg/delt/test/voltage", msg->dump(), false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                else if (!mqtt.is_connecting())
                {
                    mqtt.connect();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
            std::cout << "MQTT task exiting..." << std::endl;
        });
}

// *********************** END OF FILE ******************************* //
