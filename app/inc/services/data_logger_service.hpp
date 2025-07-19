/**
 * @file        data_logger_service.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Data Logger Service Header.
 *              A service responsible for getting the voltage measurements
 *              from the L2M Datalogger8 rev. 3 BDJI board via Modbus-TCP.
 *
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
#include <string>
#include <cstdint>
#include <optional>
#include <memory>
#include "modbus.hpp"

// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

class DataLoggerService
{
public:
    enum class Mode : uint8_t
    {
        IDLE = 0,
        RUN = 1,
        CAL = 2
    };

    explicit DataLoggerService(const std::string &ip, uint16_t port, uint8_t s_id);

    bool connect(int max_attempts = 3, int retry_delay_ms = 1000);
    void disconnect();
    bool isConnected() const noexcept { return _connected; }

    // ───── section-0 live data (read-only) ─────
    bool read_status(Mode &mode, bool &ntp, bool &autocal) const;
    bool read_active_sampling(uint32_t &sampling) const;   // [µs]
    bool read_active_epoch(uint32_t &epoch) const;         // epoch-since-2000
    bool read_active_sub_seconds(uint16_t &subsec) const;   // [ms]
    bool read_channel(uint8_t ch, float &val) const;      // ch 0-15
    bool read_board_temp(uint16_t &temp) const;            // [0.1 °C]
    bool read_board_uid(uint32_t &uid) const;
    void read_firmware(uint8_t &major, uint8_t &minor, uint16_t &build) const;

    // ───── section-1 actual configuration (read-only) ─────
    bool read_act_adc_scale(uint8_t ch, float &val) const;     // ch 0-7
    bool read_act_transd_scale(uint8_t ch, float &val) const;  // ch 0-15
    bool read_act_transd_offset(uint8_t ch, float &val) const; // ch 0-7
    bool read_act_pga_gain(uint8_t ch, uint16_t &val) const;   // ch 0-7

    // ───── section-2 set-points (read / write) ─────
    bool write_adc_scale(uint8_t ch, float val);
    bool read_adc_scale(uint8_t ch, float &val) const;

    bool write_transd_scale(uint8_t ch, float val);
    bool read_transd_scale(uint8_t ch, float &val) const;

    bool write_transd_offset(uint8_t ch, float val);
    bool read_transd_offset(uint8_t ch, float &val) const;

    bool write_pga_gain(uint8_t ch, uint16_t val);
    bool read_pga_gain(uint8_t ch, uint16_t &val) const;

    bool write_sampling_period(uint32_t us);
    bool read_sampling_period(uint32_t &us) const;

    bool write_rtc_epoch(uint32_t epoch);
    bool read_rtc_epoch(uint32_t &epoch) const;

    bool write_rtc_alarm(uint32_t epoch);
    bool read_rtc_alarm(uint32_t &epoch) const;

    // ───── section-3 commands / system control ─────
    bool write_password(uint32_t pwd);
    bool read_password(uint32_t &pwd) const;

    enum class Command : uint16_t
    {
        NO_CMD = 0,
        SET_ADC_SCALING = 1,
        SET_TRANSD_SCALING = 2,
        SET_TRANSD_OFFSET = 3,
        SET_PGA_GAIN = 4,
        STORE_CONFIG = 16,
        LOAD_CONFIG = 17,
        ERASE_CONFIG = 18
    };
    bool send_command(Command cmd);

    bool write_static_ip(uint32_t ip_be); // 0xAABBCCDD → AA.BB.CC.DD
    bool read_static_ip(uint32_t &ip_be) const; // 0xAABBCCDD → AA.BB.CC.DD

private:
    bool read_holding_u16(uint16_t addr, uint16_t &v) const;
    bool read_input_u16(uint16_t addr, uint16_t &v) const;
    bool read_holding_u32(uint16_t addr, uint32_t &v) const;
    bool read_input_u32(uint16_t addr, uint32_t &v) const;
    bool read_holding_f32(uint16_t addr, float &v) const;
    bool read_input_f32(uint16_t addr, float &v) const;
    bool write_u16(uint16_t addr, uint16_t v);
    bool write_u32(uint16_t addr, uint32_t v);
    bool write_f32(uint16_t addr, float v);

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

    // internal
    std::string _ip;
    uint16_t _port;
    uint8_t _s_id;
    bool _connected;
    std::unique_ptr<modbus> _mb;
};

// *********************** END OF FILE ******************************* //
