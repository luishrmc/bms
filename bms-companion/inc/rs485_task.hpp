#pragma once

#include "battery_snapshot.hpp"
#include "latest_battery_state.hpp"
#include "modbus_codec.hpp"

#include <boost/atomic.hpp>

namespace bms
{

    class RS485Task
    {
    public:
        struct Config
        {
            RS485Config rs485;
            int connect_retry_delay_ms{1000};
            int poll_interval_ms{1000};
            bool print_snapshot{false};
        };

        RS485Task(const Config &cfg,
                  LatestBatteryState &latest_state,
                  boost::atomic<bool> &running_flag);

        void operator()();

    private:
        Config cfg_;
        LatestBatteryState &latest_state_;
        boost::atomic<bool> &running_;
    };

} // namespace bms