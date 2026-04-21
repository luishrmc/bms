#pragma once

#include "battery_snapshot.hpp"
#include "latest_battery_state.hpp"

#include <boost/atomic.hpp>

#include <chrono>
#include <cstddef>
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
        bool retained{true};

        int reconnect_delay_ms{2000};
        int publish_interval_ms{5000};
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
        MQTTTask(const MQTTTaskConfig &cfg,
                 const LatestBatteryState &latest_state,
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
        const LatestBatteryState &latest_state_;
        boost::atomic<bool> &running_;
        MQTTTaskDiagnostics diagnostics_{};
    };

} // namespace bms