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
#include "mqtt_ctrl.hpp"
#include "data_logger_ctrl.hpp"

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //
int main()
{
    std::cout << "[Main] Starting application: " << project_name << " v" << project_version << std::endl;

    TlsConfig TLS = {
        .ca_cert = "config/mosquitto/certs/clients/node-1/ca.crt",
        .client_cert = "config/mosquitto/certs/clients/node-1/node-1.crt",
        .client_key = "config/mosquitto/certs/clients/node-1/node-1.pem",
        .verify_server = true};

    MqttService mqtt(
        MQTT_URL,
        MQTT_CLIENT_ID,
        MQTT_USER_NAME,
        MQTT_PASSWORD,
        MQTT_SOURCE_TOPIC,
        TLS,
        MQTT_DEFAULT_QOS,
        std::chrono::milliseconds(MQTT_DEFAULT_TIME_OUT_MS));

    DataLoggerService dl("127.0.0.1", 5020, 1);

    queue_service::JsonQueue dl_2_mqtt;
    queue_service::JsonQueue mqtt_2_dl;

    auto mqtt_task = start_mqtt_task(mqtt, mqtt_2_dl, dl_2_mqtt);
    auto data_logger_task = start_data_logger_task(dl, dl_2_mqtt, mqtt_2_dl);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}

// *********************** END OF FILE ******************************* //
