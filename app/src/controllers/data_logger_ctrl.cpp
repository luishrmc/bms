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

std::jthread start_data_logger_task(DataLoggerService &dl, queue_service::JsonQueue &in_queue, queue_service::JsonQueue &out_queue)
{
    return std::jthread(
        [&dl, &in_queue, &out_queue](std::stop_token stoken)
        {
            while (!stoken.stop_requested())
            {
                while (dl.connect(3))
                {
                    if (!dl.is_linked())
                    {
                        dl.link(out_queue);
                        continue;
                    }
                    dl.measurement(out_queue);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
}

// *********************** END OF FILE ******************************* //
