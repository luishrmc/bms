/**
 * @file        data_logger_ctrl.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.1.0
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

#pragma once

// ----------------------------- Includes ----------------------------- //
#include "data_logger_service.hpp"
#include <thread>

// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

std::jthread start_data_logger_task(DataLoggerService &dl, queue_service::JsonQueue &in_queue, queue_service::JsonQueue &out_queue);
// *********************** END OF FILE ******************************* //
