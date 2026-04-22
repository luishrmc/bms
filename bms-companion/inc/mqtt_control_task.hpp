#pragma once

#include "latest_regatron_state.hpp"
#include "regatron_command_state.hpp"

#include <boost/atomic.hpp>

#include <mqtt/async_client.h>

#include <string>

namespace bms
{

    /**
     * @brief MQTT settings for Regatron control input and status output.
     */
    struct MQTTControlTaskConfig
    {
        std::string server_uri{"tcp://mosquitto:1883"};
        std::string client_id{"bms-regatron-control"};
        int qos{1};
        int reconnect_delay_ms{2000};

        std::string topic_base_cmd{"bms/regatron/cmd"};
        std::string topic_base_status{"bms/regatron/status"};
    };

    /**
     * @brief Handles Regatron command intake and Regatron status publication.
     *
     * Command payloads update @ref RegatronCommandState and the current
     * Regatron status is republished periodically as retained MQTT messages.
     */
    class MQTTControlTask
    {
    public:
        /**
         * @brief Builds the Regatron MQTT control task.
         * @param cfg Broker and topic settings.
         * @param command_state Shared command state consumed by RegatronTask.
         * @param latest_state Latest Regatron feedback for status publishing.
         * @param running_flag Global lifecycle flag.
         */
        MQTTControlTask(const MQTTControlTaskConfig &cfg,
                        RegatronCommandState &command_state,
                        const LatestRegatronState &latest_state,
                        boost::atomic<bool> &running_flag);

        /**
         * @brief Task entrypoint used by the Regatron MQTT thread.
         */
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

        /**
         * @brief Parses one incoming command message and updates command state.
         * @param topic Full MQTT topic.
         * @param payload Raw payload string.
         */
        void handle_message_(const std::string &topic, const std::string &payload);
        /**
         * @brief Publishes current Regatron status values to MQTT.
         * @param client Connected MQTT client.
         */
        void publish_status_(mqtt::async_client &client);
        /**
         * @brief Ensures broker connection and command subscription.
         * @param client MQTT client instance.
         * @param opts Connection options.
         * @return True when connected and subscribed.
         */
        bool ensure_connected_(mqtt::async_client &client, mqtt::connect_options &opts);

        /**
         * @brief Parses legacy boolean command payloads.
         * @param payload Raw payload.
         * @return Parsed boolean value.
         */
        static bool parse_bool_(const std::string &payload);
        /**
         * @brief Parses legacy numeric command payloads.
         * @param payload Raw payload.
         * @return Parsed floating-point value.
         */
        static float parse_float_(const std::string &payload);

        MQTTControlTaskConfig cfg_;
        RegatronCommandState &command_state_;
        const LatestRegatronState &latest_state_;
        boost::atomic<bool> &running_;
    };

} // namespace bms
