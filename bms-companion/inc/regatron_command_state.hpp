#pragma once

#include "regatron_state.hpp"

#include <mutex>

namespace bms
{

    /**
     * @brief Thread-safe command mailbox for the Regatron FSM task.
     *
     * Level values (for example current limits) are persistent. Pulse commands
     * (start/stop/clear-fault) are consumed once by
     * snapshot_and_consume_pulses().
     */
    class RegatronCommandState
    {
    public:
        RegatronCommandState() = default;

        /**
         * @brief Enables or disables Regatron operation.
         * @param value Desired enable flag.
         */
        void set_enable(bool value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.enable = value;
        }

        /**
         * @brief Requests one start pulse for the FSM.
         */
        void request_start()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.start_requested = true;
        }

        /**
         * @brief Requests one stop pulse for the FSM.
         */
        void request_stop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.stop_requested = true;
        }

        /**
         * @brief Requests one clear-fault pulse for the FSM.
         */
        void request_clear_fault()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.clear_fault_requested = true;
        }

        /**
         * @brief Sets positive charge current demand in amperes.
         * @param value Charge current.
         */
        void set_charge_current_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.charge_current_a = value;
        }

        /**
         * @brief Sets discharge current magnitude in amperes.
         * @param value Discharge current magnitude.
         */
        void set_discharge_current_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.discharge_current_a = value;
        }

        /**
         * @brief Sets fallback cycle swap interval in seconds.
         * @param value Cycle duration in seconds.
         */
        void set_cycle_time_s(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.cycle_time_s = value;
        }

        /**
         * @brief Sets minimum battery voltage boundary for cycling.
         * @param value Voltage lower bound (V).
         */
        void set_voltage_limit_min_v(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.voltage_limit_min_v = value;
        }

        /**
         * @brief Sets maximum battery voltage boundary for cycling.
         * @param value Voltage upper bound (V).
         */
        void set_voltage_limit_max_v(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.voltage_limit_max_v = value;
        }

        /**
         * @brief Sets minimum current boundary passed to Regatron limits.
         * @param value Lower current bound (A).
         */
        void set_current_limit_min_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.current_limit_min_a = value;
        }

        /**
         * @brief Sets maximum current boundary passed to Regatron limits.
         * @param value Upper current bound (A).
         */
        void set_current_limit_max_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.current_limit_max_a = value;
        }

        /**
         * @brief Sets minimum SOC boundary for cycle reversal.
         * @param value SOC lower bound in percent.
         */
        void set_soc_min_pct(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.soc_min_pct = value;
        }

        /**
         * @brief Sets maximum SOC boundary for cycle reversal.
         * @param value SOC upper bound in percent.
         */
        void set_soc_max_pct(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.soc_max_pct = value;
        }

        /**
         * @brief Returns a full command snapshot without consuming pulses.
         * @return Current command snapshot.
         */
        [[nodiscard]] RegatronCommandSnapshot snapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return state_;
        }

        /**
         * @brief Returns a command snapshot and consumes pulse flags.
         * @return Current command snapshot with one-shot commands.
         * @warning `start_requested`, `stop_requested`, and
         *          `clear_fault_requested` are reset before returning.
         */
        RegatronCommandSnapshot snapshot_and_consume_pulses()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            RegatronCommandSnapshot copy = state_;
            state_.start_requested = false;
            state_.stop_requested = false;
            state_.clear_fault_requested = false;
            return copy;
        }

    private:
        mutable std::mutex mutex_;
        RegatronCommandSnapshot state_{};
    };

} // namespace bms
