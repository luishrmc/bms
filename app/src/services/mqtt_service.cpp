/**
 * @file        mqtt_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief
 * @version     0.1.2
 * @date        2025-07-18
 *               _   _  _____  __  __   _____
 *              | | | ||  ___||  \/  | / ____|
 *              | | | || |_   | \  / || |  __
 *              | | | ||  _|  | |\/| || | |_ |
 *              | |_| || |    | |  | || |__| |
 *               \___/ |_|    |_|  |_| \_____|
 *
 *            Universidade Federal de Minas Gerais
 *                DELT · BMS Project
 */

// ----------------------------- Includes ----------------------------- //
#include "mqtt_service.hpp"
#include "logging_service.hpp"
#include <mqtt/ssl_options.h>

#include <future>
// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

MqttService::MqttService(std::string server_uri,
                         std::string client_id,
                         std::string user_name,
                         std::string password,
                         std::string source_topic,
                         TlsConfig tls,
                         int default_qos,
                         std::chrono::milliseconds timeout)
    : client_{std::move(server_uri), std::move(client_id)},
      user_name_{user_name},
      password_{password},
      cb_{std::make_unique<Callback>(this)},
      cl_{this},
      tls_{std::move(tls)},
      default_qos_{default_qos},
      default_timeout_{timeout}
{
    client_.set_callback(*cb_);
    lwt_topic_ = source_topic + "alive";
}

MqttService::~MqttService()
{
    try
    {
        if (client_.is_connected())
            disconnect()->wait_for(default_timeout_);
    }
    catch (const std::exception &e)
    {
        app_log(LogLevel::Error, fmt::format("MQTT cleanup failed: {}", e.what()));
    }
}

static mqtt::ssl_options make_ssl(const TlsConfig &cfg)
{
    mqtt::ssl_options sslOpts{};
    sslOpts.set_trust_store(cfg.ca_cert);
    sslOpts.set_key_store(cfg.client_cert);
    sslOpts.set_private_key(cfg.client_key);
    sslOpts.set_verify(cfg.verify_server);
    sslOpts.set_error_handler(
        [](const std::string &msg)
        { std::cerr << "SSL Error: " << msg << std::endl; });
    return sslOpts;
}

void MqttService::connect()
{
    auto willmsg = mqtt::message(lwt_topic_, lwt_payload_, 0, true);
    mqtt::connect_options opts{};
    opts.set_user_name(user_name_);
    opts.set_password(password_);
    opts.set_mqtt_version(MQTTVERSION_5);
    opts.set_clean_start(true);
    opts.set_automatic_reconnect(false);
    opts.set_keep_alive_interval(60);
    opts.set_will(std::move(willmsg));
    opts.set_ssl(make_ssl(tls_));
    app_log(LogLevel::Info, fmt::format("MQTT Connecting..."));
    is_connecting_.store(true, std::memory_order_relaxed);
    client_.connect(opts)->set_action_callback(cl_);
}

mqtt::token_ptr MqttService::disconnect()
{
    app_log(LogLevel::Warn, fmt::format("MQTT Disconnecting..."));
    return client_.disconnect();
}

bool MqttService::is_connecting() const noexcept
{
    return is_connecting_.load(std::memory_order_relaxed);
}

bool MqttService::is_connected() const noexcept
{
    return client_.is_connected();
}

mqtt::delivery_token_ptr MqttService::publish(const std::string &topic,
                                              const std::string &payload,
                                              bool retained)
{
    mqtt::message_ptr msg = mqtt::make_message(source_topic_ + topic, payload, default_qos_, retained);
    return client_.publish(msg);
}

mqtt::token_ptr MqttService::subscribe(const std::string &topic, MessageHandler handler)
{
    std::scoped_lock lock(mutex_);
    handlers_[topic] = std::move(handler);
    app_log(LogLevel::Info, fmt::format("MQTT subscribe: {}", topic));
    return client_.subscribe(topic, default_qos_);
}

mqtt::token_ptr MqttService::unsubscribe(const std::string &topic)
{
    std::scoped_lock lock(mutex_);
    handlers_.erase(topic);
    return client_.unsubscribe(topic);
}

void MqttService::Callback::connected(const std::string &cause)
{
    app_log(LogLevel::Info, fmt::format("MQTT Connected Callback"));
    parent_->publish("alive", "{\"status\": \"online\"}", true);
}

void MqttService::Callback::connection_lost(const std::string &cause)
{
    app_log(LogLevel::Warn, fmt::format("MQTT Connection Lost Callback"));
}

void MqttService::Callback::message_arrived(mqtt::const_message_ptr msg)
{
    if (!parent_)
        return;

    MessageHandler handler;
    {
        std::scoped_lock lock(parent_->mutex_);
        auto it = parent_->handlers_.find(msg->get_topic());
        if (it != parent_->handlers_.end())
            handler = it->second;
    }

    if (handler)
        handler(msg); // for light handlers
}

// ************************ END OF FILE ******************************* //
