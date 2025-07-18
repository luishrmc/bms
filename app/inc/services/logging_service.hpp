/**
 * @file        logging_service.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Just a small logging wrapper for fmt library.
 * @version     0.0.1
 * @date        2025-07-18
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
#include <fmt/color.h>
#include <fmt/chrono.h>
#include <string_view>
#include <chrono>
#include <ctime>

// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

enum class LogLevel { Info, Warn, Error };

inline void log(LogLevel level, std::string_view msg) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::floor<std::chrono::seconds>(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - time).count();

    fmt::color color = fmt::color::white;
    const char* level_str = "LOG";

    switch (level) {
        case LogLevel::Info:  color = fmt::color::green;  level_str = "INFO";  break;
        case LogLevel::Warn:  color = fmt::color::yellow; level_str = "WARN";  break;
        case LogLevel::Error: color = fmt::color::red;    level_str = "ERROR"; break;
        default: break;
    }

    fmt::print(fmt::fg(color),
               "[{:%H:%M:%S}.{:03}] [{}] {}\n",
               time, ms, level_str, msg);
}

// *********************** END OF FILE ******************************* //

