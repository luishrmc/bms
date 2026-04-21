#pragma once

#include "battery_snapshot.hpp"
#include "modbus_codec.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>

#include <string>

namespace bms
{

class RS485Task
{
public:
    using SnapshotQueue = SafeQueue<BatterySnapshot>;

    struct Config
    {
        RS485Config rs485;
        int connect_retry_delay_ms{1000};
        int poll_interval_ms{1000};
    };

    RS485Task(const Config &cfg,
              SnapshotQueue &output_queue,
              boost::atomic<bool> &running_flag);

    void operator()();

private:
    Config cfg_;
    SnapshotQueue &output_queue_;
    boost::atomic<bool> &running_;
};

} // namespace bms