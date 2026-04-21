#pragma once

#include "battery_snapshot.hpp"
#include "safe_queue.hpp"

#include <boost/atomic.hpp>

#include <chrono>
#include <string>

namespace mqtt
{
    class async_client;
    class connect_options;
} // namespace mqtt

namespace bms
{

    struct MQTTTaskConfig
    {
        std::string server_uri{"tcp://mosquitto:1883"};
        std::string client_id{"bms_rs485_publisher"};
        std::string topic_battery_snapshot{"bms/battery/snapshot"};
        int qos{1};
        bool retained{false};

        int reconnect_delay_ms{1000};
        std::chrono::milliseconds wait_timeout{500};
    };

    struct MQTTTaskDiagnostics
    {
        std::size_t published_snapshots{0};
        std::size_t publish_failures{0};
        std::size_t reconnect_attempts{0};
        std::string last_error;
    };

    class MQTTTask
    {
    public:
        using SnapshotQueue = SafeQueue<BatterySnapshot>;

        MQTTTask(const MQTTTaskConfig &cfg,
                 SnapshotQueue &snapshot_queue,
                 boost::atomic<bool> &running_flag);

        void operator()();

        const MQTTTaskDiagnostics &diagnostics() const noexcept
        {
            return diagnostics_;
        }

    private:
        bool ensure_connected_(mqtt::async_client &client,
                               mqtt::connect_options &conn_opts);

        bool publish_snapshot_(mqtt::async_client &client,
                               const BatterySnapshot &snapshot);

        static std::string snapshot_to_json_(const BatterySnapshot &snapshot);

    private:
        MQTTTaskConfig cfg_;
        SnapshotQueue &snapshot_queue_;
        boost::atomic<bool> &running_;
        MQTTTaskDiagnostics diagnostics_{};
    };

} // namespace bms