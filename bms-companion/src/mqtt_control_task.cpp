#include "mqtt_control_task.hpp"

#include <nlohmann/json.hpp>

#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace bms
{

    /**
     * @brief Creates the MQTT control/status task for Regatron integration.
     * @param cfg Broker/topic settings.
     * @param command_state Shared command mailbox.
     * @param latest_state Shared Regatron status source.
     * @param running_flag Global lifecycle flag.
     */
    MQTTControlTask::MQTTControlTask(const MQTTControlTaskConfig &cfg,
                                     RegatronCommandState &command_state,
                                     const LatestRegatronState &latest_state,
                                     boost::atomic<bool> &running_flag)
        : cfg_(cfg),
          command_state_(command_state),
          latest_state_(latest_state),
          running_(running_flag)
    {
    }

    void MQTTControlTask::Callback::connected(const std::string &)
    {
        std::cout << "[MQTT-CTRL] Connected." << std::endl;
    }

    void MQTTControlTask::Callback::connection_lost(const std::string &cause)
    {
        std::cerr << "[MQTT-CTRL] Connection lost: " << cause << std::endl;
    }

    void MQTTControlTask::Callback::message_arrived(mqtt::const_message_ptr msg)
    {
        owner_.handle_message_(msg->get_topic(), msg->to_string());
    }

    bool MQTTControlTask::parse_bool_(const std::string &payload)
    {
        return payload == "1" || payload == "true" || payload == "TRUE" || payload == "on";
    }

    float MQTTControlTask::parse_float_(const std::string &payload)
    {
        return std::stof(payload);
    }

    /**
     * @brief Decodes one command message into command-state updates.
     * @param topic Full MQTT topic.
     * @param payload Raw payload received from broker.
     * @warning Invalid payloads are logged and ignored; no exception escapes.
     */
    void MQTTControlTask::handle_message_(const std::string &topic, const std::string &payload)
    {
        const auto cmd = cfg_.topic_base_cmd + "/";

        try
        {
            // New retained partial-update JSON topic:
            //   bms/regatron/cmd/set
            //
            // Example:
            // {
            //   "enable": true,
            //   "charge_current_a": 5.0,
            //   "discharge_current_a": 5.0,
            //   "cycle_time_s": 1800,
            //   "soc_min_pct": 20,
            //   "soc_max_pct": 80,
            //   "voltage_limit_min_v": 44.0,
            //   "voltage_limit_max_v": 54.2
            // }
            //
            // Partial update is supported:
            // { "charge_current_a": 8.0 }
            if (topic == cmd + "set")
            {
                const auto j = nlohmann::json::parse(payload);

                if (!j.is_object())
                {
                    throw std::runtime_error("cmd/set payload must be a JSON object");
                }

                auto get_bool = [](const nlohmann::json &v) -> bool
                {
                    if (v.is_boolean())
                    {
                        return v.get<bool>();
                    }
                    if (v.is_number_integer())
                    {
                        return v.get<int>() != 0;
                    }
                    if (v.is_string())
                    {
                        const auto s = v.get<std::string>();
                        return s == "1" || s == "true" || s == "TRUE" || s == "on";
                    }
                    throw std::runtime_error("invalid boolean field");
                };

                auto get_float = [](const nlohmann::json &v) -> float
                {
                    if (v.is_number())
                    {
                        return v.get<float>();
                    }
                    if (v.is_string())
                    {
                        return std::stof(v.get<std::string>());
                    }
                    throw std::runtime_error("invalid numeric field");
                };

                if (j.contains("enable"))
                {
                    command_state_.set_enable(get_bool(j.at("enable")));
                }
                if (j.contains("charge_current_a"))
                {
                    command_state_.set_charge_current_a(get_float(j.at("charge_current_a")));
                }
                if (j.contains("discharge_current_a"))
                {
                    command_state_.set_discharge_current_a(get_float(j.at("discharge_current_a")));
                }
                if (j.contains("cycle_time_s"))
                {
                    command_state_.set_cycle_time_s(get_float(j.at("cycle_time_s")));
                }
                if (j.contains("voltage_limit_min_v"))
                {
                    command_state_.set_voltage_limit_min_v(get_float(j.at("voltage_limit_min_v")));
                }
                if (j.contains("voltage_limit_max_v"))
                {
                    command_state_.set_voltage_limit_max_v(get_float(j.at("voltage_limit_max_v")));
                }
                if (j.contains("current_limit_min_a"))
                {
                    command_state_.set_current_limit_min_a(get_float(j.at("current_limit_min_a")));
                }
                if (j.contains("current_limit_max_a"))
                {
                    command_state_.set_current_limit_max_a(get_float(j.at("current_limit_max_a")));
                }
                if (j.contains("soc_min_pct"))
                {
                    command_state_.set_soc_min_pct(get_float(j.at("soc_min_pct")));
                }
                if (j.contains("soc_max_pct"))
                {
                    command_state_.set_soc_max_pct(get_float(j.at("soc_max_pct")));
                }

                return;
            }

            // New action topic:
            //   bms/regatron/cmd/action
            //
            // Supported payloads:
            //   {"action":"start"}
            //   {"action":"stop"}
            //   {"action":"clear_fault"}
            //
            // Also accepts plain string payloads:
            //   start
            //   stop
            //   clear_fault
            if (topic == cmd + "action")
            {
                std::string action;

                try
                {
                    const auto j = nlohmann::json::parse(payload);
                    if (j.is_object() && j.contains("action") && j.at("action").is_string())
                    {
                        action = j.at("action").get<std::string>();
                    }
                    else if (j.is_string())
                    {
                        action = j.get<std::string>();
                    }
                    else
                    {
                        throw std::runtime_error("invalid action payload");
                    }
                }
                catch (const nlohmann::json::parse_error &)
                {
                    action = payload;
                }

                if (action == "start")
                {
                    command_state_.request_start();
                }
                else if (action == "stop")
                {
                    command_state_.request_stop();
                }
                else if (action == "clear_fault")
                {
                    command_state_.request_clear_fault();
                }
                else
                {
                    throw std::runtime_error("unknown action: " + action);
                }

                return;
            }

            // Optional backward compatibility with old per-topic style.
            // You can remove this later if you want to fully migrate.
            if (topic == cmd + "enable")
            {
                command_state_.set_enable(parse_bool_(payload));
            }
            else if (topic == cmd + "start" && parse_bool_(payload))
            {
                command_state_.request_start();
            }
            else if (topic == cmd + "stop" && parse_bool_(payload))
            {
                command_state_.request_stop();
            }
            else if (topic == cmd + "clear_fault" && parse_bool_(payload))
            {
                command_state_.request_clear_fault();
            }
            else if (topic == cmd + "charge_current_a")
            {
                command_state_.set_charge_current_a(parse_float_(payload));
            }
            else if (topic == cmd + "discharge_current_a")
            {
                command_state_.set_discharge_current_a(parse_float_(payload));
            }
            else if (topic == cmd + "cycle_time_s")
            {
                command_state_.set_cycle_time_s(parse_float_(payload));
            }
            else if (topic == cmd + "voltage_limit_min_v")
            {
                command_state_.set_voltage_limit_min_v(parse_float_(payload));
            }
            else if (topic == cmd + "voltage_limit_max_v")
            {
                command_state_.set_voltage_limit_max_v(parse_float_(payload));
            }
            else if (topic == cmd + "current_limit_min_a")
            {
                command_state_.set_current_limit_min_a(parse_float_(payload));
            }
            else if (topic == cmd + "current_limit_max_a")
            {
                command_state_.set_current_limit_max_a(parse_float_(payload));
            }
            else if (topic == cmd + "soc_min_pct")
            {
                command_state_.set_soc_min_pct(parse_float_(payload));
            }
            else if (topic == cmd + "soc_max_pct")
            {
                command_state_.set_soc_max_pct(parse_float_(payload));
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[MQTT-CTRL] Invalid payload for topic '" << topic
                      << "': " << payload << " (" << e.what() << ")" << std::endl;
        }
    }

    /**
     * @brief Connects to MQTT and subscribes to all command topics.
     * @return True when connected and subscription succeeds.
     */
    bool MQTTControlTask::ensure_connected_(mqtt::async_client &client, mqtt::connect_options &opts)
    {
        if (client.is_connected())
        {
            return true;
        }

        while (running_)
        {
            try
            {
                auto tok = client.connect(opts);
                tok->wait();

                client.subscribe(cfg_.topic_base_cmd + "/#", cfg_.qos)->wait();
                return true;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[MQTT-CTRL] Connect failed: " << e.what() << std::endl;
                boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(cfg_.reconnect_delay_ms));
            }
        }

        return false;
    }

    /**
     * @brief Publishes retained Regatron status fields and JSON summary.
     * @note Publication is skipped until at least one Regatron snapshot exists.
     */
    void MQTTControlTask::publish_status_(mqtt::async_client &client)
    {
        const auto maybe = latest_state_.get();
        if (!maybe.has_value() || !client.is_connected())
        {
            return;
        }

        const auto &s = *maybe;
        const auto base = cfg_.topic_base_status + "/";

        nlohmann::json summary;
        summary["state"] = to_string(s.fsm_state);
        summary["actual_switch"] = to_string(s.actual_switch);
        summary["actual_voltage_v"] = s.actual_voltage_v;
        summary["actual_current_a"] = s.actual_current_a;
        summary["actual_power_kw"] = s.actual_power_kw;
        summary["fault_active"] = s.fault_active;
        summary["fault_code"] = s.fault_code;
        summary["fault_msg_id"] = s.fault_msg_id;
        summary["cycle_active"] = s.cycle_active;
        summary["cycle_elapsed_s"] = s.cycle_elapsed_s;
        summary["info"] = s.info;

        client.publish(base + "state", to_string(s.fsm_state), cfg_.qos, true);
        client.publish(base + "actual_voltage_v", std::to_string(s.actual_voltage_v), cfg_.qos, true);
        client.publish(base + "actual_current_a", std::to_string(s.actual_current_a), cfg_.qos, true);
        client.publish(base + "actual_power_kw", std::to_string(s.actual_power_kw), cfg_.qos, true);
        client.publish(base + "actual_switch", to_string(s.actual_switch), cfg_.qos, true);
        client.publish(base + "fault_active", s.fault_active ? "1" : "0", cfg_.qos, true);
        client.publish(base + "fault_code", std::to_string(s.fault_code), cfg_.qos, true);
        client.publish(base + "fault_msg_id", std::to_string(s.fault_msg_id), cfg_.qos, true);
        client.publish(base + "summary", summary.dump(), cfg_.qos, true);
    }

    /**
     * @brief Task loop for MQTT command intake and 1 Hz status publication.
     */
    void MQTTControlTask::operator()()
    {
        mqtt::async_client client(cfg_.server_uri, cfg_.client_id);
        mqtt::connect_options opts;
        opts.set_clean_session(false);
        opts.set_automatic_reconnect(true);

        Callback cb(*this);
        client.set_callback(cb);

        if (!ensure_connected_(client, opts))
        {
            return;
        }

        while (running_)
        {
            try
            {
                if (!ensure_connected_(client, opts))
                {
                    break;
                }

                publish_status_(client);
            }
            catch (const std::exception &e)
            {
                std::cerr << "[MQTT-CTRL] publish_status failed: " << e.what() << std::endl;
            }

            boost::this_thread::sleep_for(boost::chrono::seconds(1));
        }

        try
        {
            if (client.is_connected())
            {
                client.disconnect()->wait();
            }
        }
        catch (...)
        {
        }

        std::cout << "[MQTT-CTRL] Task stopped." << std::endl;
    }

} // namespace bms
