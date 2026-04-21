#pragma once

#include "regatron_state.hpp"

#include <mutex>

namespace bms
{

    class RegatronCommandState
    {
    public:
        RegatronCommandState() = default;

        void set_enable(bool value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.enable = value;
        }

        void request_start()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.start_requested = true;
        }

        void request_stop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.stop_requested = true;
        }

        void request_clear_fault()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.clear_fault_requested = true;
        }

        void set_charge_current_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.charge_current_a = value;
        }

        void set_discharge_current_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.discharge_current_a = value;
        }

        void set_cycle_time_s(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.cycle_time_s = value;
        }

        void set_voltage_limit_min_v(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.voltage_limit_min_v = value;
        }

        void set_voltage_limit_max_v(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.voltage_limit_max_v = value;
        }

        void set_current_limit_min_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.current_limit_min_a = value;
        }

        void set_current_limit_max_a(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.current_limit_max_a = value;
        }

        void set_soc_min_pct(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.soc_min_pct = value;
        }

        void set_soc_max_pct(float value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.soc_max_pct = value;
        }

        [[nodiscard]] RegatronCommandSnapshot snapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return state_;
        }

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