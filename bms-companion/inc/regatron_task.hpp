#pragma once

#include "latest_battery_state.hpp"
#include "latest_regatron_state.hpp"
#include "regatron_command_state.hpp"

#include <boost/atomic.hpp>
#include <string>

#include <boost/atomic.hpp>

#include <string>

namespace bms
{

    class RegatronTask
    {
    public:
        struct Config
        {
            std::string can_ifname{"can0"};

            int connect_retry_delay_ms{1000};
            int base_period_ms{10};

            int wait_delay_ms{1000};
            int standby2_delay_ms{1000};
            int standby1_timeout_ms{5000};
            int can_rx_timeout_ms{2000};

            float default_voltage_v{48.0F};
            float default_u_slope_v_per_s{2.0F};
            float default_i_slope_a_per_s{10.0F};
            float default_p_slope_kw_per_s{0.0F};

            float default_supervision_u_min_v{44.0F};
            float default_supervision_u_max_v{54.2F};
            float default_supervision_i_min_a{-10.0F};
            float default_supervision_i_max_a{10.0F};
            float default_supervision_p_min_kw{-10.0F};
            float default_supervision_p_max_kw{10.0F};
        };

        RegatronTask(const Config &cfg,
                     RegatronCommandState &command_state,
                     LatestRegatronState &latest_state,
                     const LatestBatteryState &latest_battery_state,
                     boost::atomic<bool> &running_flag);

        void operator()();

    private:
        Config cfg_;
        RegatronCommandState &command_state_;
        const LatestBatteryState &latest_battery_state_;
        LatestRegatronState &latest_state_;
        boost::atomic<bool> &running_;
    };

} // namespace bms