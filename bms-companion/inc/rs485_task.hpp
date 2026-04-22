#pragma once

#include "battery_snapshot.hpp"
#include "latest_battery_state.hpp"
#include "modbus_codec.hpp"

#include <boost/atomic.hpp>

namespace bms
{

    /**
     * @brief Periodic RS485 poller that updates the latest battery snapshot.
     */
    class RS485Task
    {
    public:
        /**
         * @brief Configuration for Modbus polling and reconnect cadence.
         */
        struct Config
        {
            RS485Config rs485;
            int connect_retry_delay_ms{1000};
            int poll_interval_ms{1000};
            bool print_snapshot{false};
        };

        /**
         * @brief Builds the RS485 polling task.
         * @param cfg Polling and serial settings.
         * @param latest_state Destination for decoded snapshots.
         * @param running_flag Global lifecycle flag.
         */
        RS485Task(const Config &cfg,
                  LatestBatteryState &latest_state,
                  boost::atomic<bool> &running_flag);

        /**
         * @brief Task entrypoint used by the RS485 thread.
         */
        void operator()();

    private:
        Config cfg_;
        LatestBatteryState &latest_state_;
        boost::atomic<bool> &running_;
    };

} // namespace bms
