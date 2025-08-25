/**
 * @file        data_logger_ctrl.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.3.0
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

std::jthread start_data_logger_task(DataLoggerService &dl, SPSCQueue<std::array<float, 16>> &influx_queue)
{
    return std::jthread(
        [&dl, &influx_queue](std::stop_token stoken)
        {
            while (!stoken.stop_requested())
            {
                while (dl.connect(3))
                {
                    if (dl.read_all_channels() == 0)
                    {
                        influx_queue.try_push(dl._adc_channels);
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
}

// *********************** END OF FILE ******************************* //
