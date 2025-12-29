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
#include <sstream>
#include <iomanip>
// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

void data_to_db(std::string board_uid, std::array<float, 8> &data, MPSCQueue<std::string> &influx_queue);
std::jthread start_data_logger_task(DataLoggerService &dl, MPSCQueue<std::string> &influx_queue)
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
                        data_to_db(dl._board_uid, dl._adc_channels, influx_queue);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    else
                        dl.disconnect();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
}

void data_to_db(std::string board_uid, std::array<float, 8> &data, MPSCQueue<std::string> &influx_queue)
{
    std::ostringstream lp_line;
    lp_line << "bank0,sensor_id=" << board_uid << " ";
    lp_line << std::fixed << std::setprecision(5);

    for (std::size_t ch = 0; ch < data.size(); ++ch)
    {
        lp_line << "ch" << (ch) << "=" << data[ch];
        if (ch + 1 < data.size())
            lp_line << ",";
    }
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    lp_line << " " << ns;
    influx_queue.try_push(lp_line.str());
}

// *********************** END OF FILE ******************************* //
