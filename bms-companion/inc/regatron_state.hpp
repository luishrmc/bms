#pragma once

#include <boost/chrono.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace bms
{

    /**
     * @brief Internal FSM states for Regatron control sequencing.
     */
    enum class RegatronFsmState : std::uint8_t
    {
        INIT = 0,
        OFF,
        WAIT,
        STANDBY1,
        STANDBY2,
        CHARGE,
        DISCHARGE,
        ERROR
    };

    /**
     * @brief Switch state reported by Regatron CAN feedback.
     */
    enum class RegatronSwitchState : std::uint8_t
    {
        OFF = 0,
        STANDBY = 1,
        ON = 2,
        ERROR = 5,
        UNKNOWN = 255
    };

    /**
     * @brief Regatron control mode reported by feedback frames.
     */
    enum class RegatronControlMode : std::uint8_t
    {
        VOLTAGE = 1,
        CURRENT = 2,
        POWER = 3
    };

    /**
     * @brief Converts an FSM state to text for logs and MQTT payloads.
     * @param s FSM state.
     * @return Constant state label.
     */
    inline const char *to_string(RegatronFsmState s) noexcept
    {
        switch (s)
        {
        case RegatronFsmState::INIT:
            return "INIT";
        case RegatronFsmState::OFF:
            return "OFF";
        case RegatronFsmState::WAIT:
            return "WAIT";
        case RegatronFsmState::STANDBY1:
            return "STANDBY1";
        case RegatronFsmState::STANDBY2:
            return "STANDBY2";
        case RegatronFsmState::CHARGE:
            return "CHARGE";
        case RegatronFsmState::DISCHARGE:
            return "DISCHARGE";
        case RegatronFsmState::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief Converts a Regatron switch state to text.
     * @param s Switch state.
     * @return Constant switch-state label.
     */
    inline const char *to_string(RegatronSwitchState s) noexcept
    {
        switch (s)
        {
        case RegatronSwitchState::OFF:
            return "OFF";
        case RegatronSwitchState::STANDBY:
            return "STANDBY";
        case RegatronSwitchState::ON:
            return "ON";
        case RegatronSwitchState::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief Latest decoded Regatron runtime feedback and FSM context.
     */
    struct RegatronStatusSnapshot
    {
        std::chrono::system_clock::time_point timestamp{};
        RegatronFsmState fsm_state{RegatronFsmState::INIT};

        bool can_online{false};
        bool cycle_active{false};

        float actual_voltage_v{0.0F};
        float actual_current_a{0.0F};
        float actual_power_kw{0.0F};

        float actual_set_voltage_v{0.0F};
        float actual_set_current_a{0.0F};

        float commanded_voltage_v{0.0F};
        float commanded_current_a{0.0F};

        RegatronSwitchState actual_switch{RegatronSwitchState::UNKNOWN};
        RegatronControlMode actual_control_mode{RegatronControlMode::CURRENT};

        bool fault_active{false};
        std::uint8_t fault_code{0U};
        std::uint8_t fault_msg_id{0U};

        std::uint32_t watchdog{0U};
        std::uint32_t cycle_elapsed_s{0U};

        std::string info;
    };

    /**
     * @brief User command and boundary setpoints consumed by RegatronTask.
     *
     * @note `start_requested`, `stop_requested`, and `clear_fault_requested`
     *       are pulse-style flags consumed by
     *       RegatronCommandState::snapshot_and_consume_pulses().
     */
    struct RegatronCommandSnapshot
    {
        bool enable{false};

        bool start_requested{false};
        bool stop_requested{false};
        bool clear_fault_requested{false};

        float charge_current_a{10.0F};
        float discharge_current_a{10.0F};
        float cycle_time_s{100.0F};

        float voltage_limit_min_v{44.0F};
        float voltage_limit_max_v{55.0F};
        float current_limit_min_a{-100.0F};
        float current_limit_max_a{100.0F};

        float soc_min_pct{0.0F};
        float soc_max_pct{100.0F};
    };

} // namespace bms
