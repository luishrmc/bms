#pragma once

#include "latest_regatron_state.hpp"
#include "regatron_command_state.hpp"

#include <boost/atomic.hpp>

#include <mqtt/async_client.h>

#include <string>

namespace bms
{

    struct MQTTControlTaskConfig
    {
        std::string server_uri{"tcp://mosquitto:1883"};
        std::string client_id{"bms-regatron-control"};
        int qos{1};
        int reconnect_delay_ms{2000};

        std::string topic_base_cmd{"bms/regatron/cmd"};
        std::string topic_base_status{"bms/regatron/status"};
    };

    class MQTTControlTask
    {
    public:
        MQTTControlTask(const MQTTControlTaskConfig &cfg,
                        RegatronCommandState &command_state,
                        const LatestRegatronState &latest_state,
                        boost::atomic<bool> &running_flag);

        void operator()();

    private:
        class Callback final : public virtual mqtt::callback
        {
        public:
            explicit Callback(MQTTControlTask &owner)
                : owner_(owner)
            {
            }

            void connected(const std::string &) override;
            void connection_lost(const std::string &cause) override;
            void message_arrived(mqtt::const_message_ptr msg) override;

        private:
            MQTTControlTask &owner_;
        };

        void handle_message_(const std::string &topic, const std::string &payload);
        void publish_status_(mqtt::async_client &client);
        bool ensure_connected_(mqtt::async_client &client, mqtt::connect_options &opts);

        static bool parse_bool_(const std::string &payload);
        static float parse_float_(const std::string &payload);

        MQTTControlTaskConfig cfg_;
        RegatronCommandState &command_state_;
        const LatestRegatronState &latest_state_;
        boost::atomic<bool> &running_;
    };

} // namespace bms