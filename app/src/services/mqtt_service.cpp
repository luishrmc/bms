/**
 * @file        mqtt_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief
 * @version     0.0.1
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
#include <thread>
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
                         TlsConfig tls,
                         int qos,
                         std::chrono::milliseconds timeout)
    : client_{std::move(server_uri), std::move(client_id)},
      user_name_{user_name},
      password_{password},
      cb_{std::make_unique<Callback>(this)},
      tls_{std::move(tls)},
      default_qos_{qos},
      default_timeout_{timeout}
{
    client_.set_callback(*cb_);
}

MqttService::~MqttService()
{
    try
    {
        if (is_connected())
        {
            disconnect().wait();
        }
    }
    catch (const std::exception &e)
    {
        log(LogLevel::Error, fmt::format("MQTT cleanup failed: {}", e.what()));
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

std::future<void> MqttService::connect()
{
    std::promise<void> prom;
    auto fut = prom.get_future();
    auto willmsg = mqtt::message(lwt_topic_, lwt_payload_, 0, true);
    mqtt::connect_options connOpts{};
    connOpts.set_user_name(user_name_);
    connOpts.set_password(password_);
    connOpts.set_mqtt_version(MQTTVERSION_5);
    connOpts.set_clean_start(true);
    connOpts.set_automatic_reconnect(true);
    connOpts.set_keep_alive_interval(20);
    connOpts.set_will(std::move(willmsg));
    connOpts.set_ssl(make_ssl(tls_));

    // Launch async worker so API returns immediately
    std::thread{[this, connOpts, p = std::move(prom)]() mutable
                {
                    try
                    {
                        client_.connect(connOpts)->wait();
                        p.set_value();
                    }
                    catch (...)
                    {
                        p.set_exception(std::current_exception());
                    }
                }}
        .detach();

    return fut;
}

std::future<void> MqttService::disconnect()
{
    std::promise<void> prom;
    auto fut = prom.get_future();

    std::thread{[this, p = std::move(prom)]() mutable
                {
                    try
                    {
                        client_.disconnect()->wait();
                        p.set_value();
                    }
                    catch (...)
                    {
                        p.set_exception(std::current_exception());
                    }
                }}
        .detach();

    return fut;
}

bool MqttService::is_connected() const noexcept
{
    return client_.is_connected();
}

std::future<void> MqttService::publish(const std::string &topic,
                                       const std::string &payload,
                                       int qos,
                                       bool retained)
{
    std::promise<void> prom;
    auto fut = prom.get_future();

    int effective_qos = (qos >= 0) ? qos : default_qos_;
    mqtt::message_ptr msg = mqtt::make_message(topic, payload, effective_qos, retained);

    std::thread{[this, msg = std::move(msg), p = std::move(prom)]() mutable {
        try {
            client_.publish(msg)->wait_for(default_timeout_);
            p.set_value();
        }
        catch (...) {
            p.set_exception(std::current_exception());
        }
    }}.detach();

    return fut;
}

std::future<void> MqttService::subscribe(const std::string &topic,
                                         MessageHandler handler,
                                         int qos)
{
    std::promise<void> prom;
    auto fut = prom.get_future();

    int effective_qos = (qos >= 0) ? qos : default_qos_;
    {
        std::scoped_lock lock(mutex_);
        handlers_[topic] = std::move(handler);
    }

    std::thread{[this, topic, qos = effective_qos, p = std::move(prom)]() mutable {
        try {
            client_.subscribe(topic, qos)->wait_for(default_timeout_);
            p.set_value();
        }
        catch (...) {
            p.set_exception(std::current_exception());
        }
    }}.detach();

    return fut;
}

std::future<void> MqttService::unsubscribe(const std::string &topic)
{
    std::promise<void> prom;
    auto fut = prom.get_future();

    {
        std::scoped_lock lock(mutex_);
        handlers_.erase(topic);
    }

    std::thread{[this, topic, p = std::move(prom)]() mutable {
        try {
            client_.unsubscribe(topic)->wait_for(default_timeout_);
            p.set_value();
        }
        catch (...) {
            p.set_exception(std::current_exception());
        }
    }}.detach();

    return fut;
}

// --------------------- MQTT Callbacks -------------------- //

void MqttService::Callback::connected(const std::string &cause)
{
    log(LogLevel::Info, fmt::format("MQTT connected: {}", cause));
}

void MqttService::Callback::connection_lost(const std::string &cause)
{
    log(LogLevel::Warn, fmt::format("MQTT connection lost: {}", cause));
    if (parent_)
    {
        // Best‑effort automatic reconnect
        try
        {
            std::ignore = parent_->connect();
        }
        catch (const std::exception &e)
        {
            log(LogLevel::Error, fmt::format("Reconnect attempt failed: {}", e.what()));
        }
    }
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
    {
        // Dispatch on a detached thread to keep callback non‑blocking
        std::thread{[handler, m = std::move(msg)]
                    { handler(m); }}
            .detach();
    }
}

// *********************** END OF FILE ******************************* //
