/**
 * @file        mqtt_service.hpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Thread‑safe wrapper around Eclipse Paho MQTT C++ (v1.4+) for
 *              MQTT v5 over TLS. It offers non‑blocking connect, publish and
 *              subscribe helpers to integrate seamlessly in a multi‑threaded
 *              BMS data‑logger application.
 *
 *              Design goals
 *              -------------
 *              • Provide a thin façade that keeps Paho asynchronous semantics
 *                while hiding boilerplate (TLS setup, reconnect strategy).
 *              • Ensure thread‑safety by guarding the public API with a
 *                single `std::mutex` and using Paho‟s async tokens ‑ no busy
 *                waits.
 *              • Allow clients to register topic‑specific message handlers that
 *                are dispatched on the internal callback thread.
 *              • Keep blocking to a minimum; long operations (connect, publish)
 *                return immediately – status can be checked through futures.
 *
 * @version     0.0.1
 * @date        2025‑07‑19
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

#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include <mqtt/async_client.h>

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------------
// Public Types
// -----------------------------------------------------------------------------

/**
 * @brief TLS credential bundle required to establish a secure MQTT session.
 */
struct TlsConfig
{
    std::string ca_cert{};     ///< Path to CA certificate (PEM).
    std::string client_cert{}; ///< Path to client certificate (PEM).
    std::string client_key{};  ///< Path to client private key (PEM).
    bool verify_server{true};  ///< Enforce broker certificate validation.
};

/**
 * @brief Convenience alias for message callback.
 */
using MessageHandler = std::function<void(const mqtt::const_message_ptr &)>;

// -----------------------------------------------------------------------------
// Class Declaration
// -----------------------------------------------------------------------------

class MqttService
{
public:
    /**
     * @brief Construct a new MQTT service object.
     *
     * @param server_uri  URI of the broker (e.g. "mqtts://host:8883").
     * @param client_id   Unique client identifier.
     * @param tls         TLS credential bundle.
     * @param qos         Default QoS used when none is specified (0‑2).
     * @param timeout     Default timeout for async operations.
     */
    MqttService(std::string server_uri,
                std::string client_id,
                std::string user_name,
                std::string password,
                TlsConfig tls,
                int default_qos = 1,
                std::chrono::milliseconds timeout = std::chrono::seconds(10));

    /** @brief Destructor – disconnects gracefully if still connected. */
    ~MqttService();

    // ---------------------------------------------------------------------
    // Connection management (non‑blocking)
    // ---------------------------------------------------------------------

    mqtt::token_ptr connect();
    mqtt::token_ptr disconnect();
    [[nodiscard]] bool is_connected() const noexcept;

    // ---------------------------------------------------------------------
    // Publish / Subscribe interface
    // ---------------------------------------------------------------------

    /**
     * @brief Publish a message (non‑blocking).
     *
     * @param topic     Destination topic.
     * @param payload   Arbitrary byte buffer.
     * @param retained  Retain flag.
     * @return `std::future<void>` signalling completion or failure.
     */
    mqtt::delivery_token_ptr publish(const std::string &topic,
                              const std::string &payload,
                              bool retained = false);

    /**
     * @brief Subscribe to a topic and register a handler (non‑blocking).
     *
     * @param topic     Topic filter (may include wildcards).
     * @param handler   Callback executed on message arrival.
     * @param qos       QoS level (0‑2). Default = constructor‐provided `qos_`.
     * @return `std::future<void>` signalling subscription completion.
     */
    mqtt::token_ptr subscribe(const std::string &topic, MessageHandler handler);

    /**
     * @brief Remove an existing subscription.
     *
     * @param topic Topic filter.
     * @return `std::future<void>` signalling un‑subscribe completion.
     */
    mqtt::token_ptr unsubscribe(const std::string &topic);

    const std::chrono::milliseconds default_timeout_;
private:
    // -----------------------------------------------------------------
    // Internal helper types
    // -----------------------------------------------------------------

    /**
     * @brief Paho callback adapter – forwards events to `MqttService`.
     *
     * This nested class is required because Paho expects the callback
     * object to out‑live the client. It captures a raw pointer to the
     * parent (non‑owning).
     */
    class Callback final : public virtual mqtt::callback
    {
    public:
        explicit Callback(MqttService *parent) : parent_{parent} {}

        void connected(const std::string & /*cause*/) override;
        void connection_lost(const std::string &cause) override;
        void message_arrived(mqtt::const_message_ptr msg) override;
        void delivery_complete(mqtt::delivery_token_ptr) override {}

    private:
        MqttService *parent_;
    };

    // -----------------------------------------------------------------
    // Data members (mutable state guarded by `mutex_`)
    // -----------------------------------------------------------------

    mutable std::mutex mutex_{}; ///< Guards below.
    mqtt::async_client client_;
    std::unique_ptr<Callback> cb_;
    std::unordered_map<std::string, MessageHandler> handlers_{};
    TlsConfig tls_;

    // Immutable configuration
    const int default_qos_;
    const std::string user_name_{"lumac"};
    const std::string password_{"128Parsecs!"};
    const std::string lwt_topic_{"events/disconnect"};
    const std::string lwt_payload_{"Last will and testament."};
};

// ************************* END OF FILE ******************************* //
