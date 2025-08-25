/**
 * @file        data_logger_service.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Header.
 *              A service responsible for getting the voltage measurements
 *              from the L2M Datalogger8 rev. 3 BDJI board via Modbus-TCP.
 *
 * @version     0.3.0
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
#include <string>
#include <cstdint>
#include <optional>
#include "modbus.hpp"
#include <future>
#include <array>

// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

class DataLoggerService : public modbus
{
public:
    using modbus::modbus;
    DataLoggerService(const std::string &ip, uint16_t port, uint8_t s_id);

    uint8_t _mode{0};
    bool _ntp{false};
    bool _autocal{false};
    uint32_t _sampling_period{0};       // [µs]
    std::string _rtc_epoch{""};         // epoch-since-2000
    uint16_t _board_temp{0};            // [°C]
    std::string _board_uid{""};         // [UID]
    std::string _fw_version{""};        // firmware version
    uint16_t _fw_build{0};              // firmware build number
    std::array<float, 16> _adc_channels; // ADC channels

    bool connect(uint8_t max_attempts);
    void disconnect();
    bool is_linked() const { return _is_linked; }

    /* --------------------------- Section 0 (read-only) --------------------------- */
    int read_status();
    int read_channel(uint8_t ch); // ch 0-15
    int read_board_temp();        // [0.1 °C]
    int read_board_uid();
    int read_firmware_version();

    /* --------------------------- Section 4 (read all) --------------------------- */
    int read_all_channels();
    int read_all_config();

private:
    std::mutex _mb_mtx;
    bool _is_linked{false};
    int write_float(uint8_t ch, uint16_t addr, float val);
    int read_float(uint8_t ch, uint16_t addr, float &dst);

    // Section-0
    static constexpr uint16_t REG_STATUS = 0;       // Section 0
    static constexpr uint16_t REG_ACT_SAMPLING = 1; // 1-2
    static constexpr uint16_t REG_ACT_EPOCH = 3;    // 3-4
    static constexpr uint16_t REG_ACT_SUBSEC = 5;   // 5
    static constexpr uint16_t REG_CH_BASE = 6;      // 6-37 (2 words each)
    static constexpr uint16_t REG_BOARD_TEMP = 38;  // 38
    static constexpr uint16_t REG_BOARD_UID = 96;   // 96-97
    static constexpr uint16_t REG_FW_VERSION = 98;  // 98
    static constexpr uint16_t REG_FW_BUILD = 99;    // 99

    // Section-1
    static constexpr uint16_t REG_ACT_ADC_SCALE = 100;    // 100-115 (2 words each)
    static constexpr uint16_t REG_ACT_TRANSD_SCALE = 116; // 116-147 (2 words each)
    static constexpr uint16_t REG_ACT_TRANSD_OFFS = 148;  // 148-179 (2 words each)
    static constexpr uint16_t REG_ACT_PGA_GAIN = 180;     // 180-187 (1/2 word each)

    // Section-2
    static constexpr uint16_t REG_SET_ADC_SCALE = 200;    // 200-215 (2 words each)
    static constexpr uint16_t REG_SET_TRANSD_SCALE = 216; // 216-247 (2 words each)
    static constexpr uint16_t REG_SET_TRANSD_OFFS = 248;  // 248-279 (2 words each)
    static constexpr uint16_t REG_SET_PGA_GAIN = 280;     // 280-287 (1/2 word each)
    static constexpr uint16_t REG_SET_SAMPLING = 288;     // 288-289 (1 word)
    static constexpr uint16_t REG_SET_RTC_EPOCH = 290;    // 290-291 (1 word)
    static constexpr uint16_t REG_SET_RTC_ALARM = 292;    // 292-293 (1 word)

    // Section-3
    static constexpr uint16_t REG_PASSWORD = 300;  // 300-301 (1 word)
    static constexpr uint16_t REG_COMMAND = 302;   // 302 (1/2 word)
    static constexpr uint16_t REG_STATIC_IP = 303; // 303-304 (1 word)
};
