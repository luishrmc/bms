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
#include "nlohmann/json.hpp"
#include "config.hpp"

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

using json = nlohmann::json;
void check_input_queue(MqttService &mqtt, queue_service::JsonQueue &in_queue);

std::jthread start_mqtt_task(MqttService& mqtt, queue_service::JsonQueue& in_queue, queue_service::JsonQueue& out_queue)
{
    return std::jthread(
        [&mqtt, &in_queue, &out_queue](std::stop_token stoken)
        {
            while (!stoken.stop_requested())
            {
                if (mqtt.is_connected())
                {
                    check_input_queue(mqtt, in_queue);
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

void check_input_queue(MqttService &mqtt, queue_service::JsonQueue &in_queue)
{
    auto msg = in_queue.try_pop();
    if (msg != std::nullopt)
    {
        std::string topic_msg = (*msg)["topic"].get<std::string>();
        msg->erase("topic");
        mqtt.publish(topic_msg, msg->dump(), false);
    }
}

// *********************** END OF FILE ******************************* //
