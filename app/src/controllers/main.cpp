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
#include "mqtt_ctrl.hpp"

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //
int main()
{
    std::cout << "[Main] Starting application: " << project_name << " v" << project_version << std::endl;

    TlsConfig tls = {
        .ca_cert = "config/mosquitto/certs/clients/node-1/ca.crt",
        .client_cert = "config/mosquitto/certs/clients/node-1/node-1.crt",
        .client_key = "config/mosquitto/certs/clients/node-1/node-1.pem",
        .verify_server = true};

    MqttService mqtt(
        "mqtts://mosquitto:8883",
        "ssl_publish_cpp",
        "lumac",
        "128Parsecs!",
        tls,
        1,
        std::chrono::seconds(10));

    auto mqtt_task = start_mqtt_task(mqtt);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}

// *********************** END OF FILE ******************************* //
