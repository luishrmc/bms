#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_test_case_info.hpp>
#include "mqtt_service.hpp"

TlsConfig tls = {
    .ca_cert = "./../../config/mosquitto/certs/clients/node-1/ca.crt",
    .client_cert = "./../../config/mosquitto/certs/clients/node-1/node-1.crt",
    .client_key = "./../../config/mosquitto/certs/clients/node-1/node-1.pem",
    .verify_server = true};

TEST_CASE("MQTT Service Connection and Disconnection")
{
    MqttService mqttService(
        "mqtts://mosquitto:8883",
        "ssl_publish_cpp",
        "lumac",
        "128Parsecs!",
        tls,
        1,
        std::chrono::seconds(10));

    SECTION("Connect to MQTT broker")
    {
        REQUIRE_NOTHROW(mqttService.connect().wait());
        REQUIRE(mqttService.is_connected());
    }

    SECTION("Disconnect from MQTT broker")
    {
        REQUIRE_NOTHROW(mqttService.disconnect().wait());
        REQUIRE_FALSE(mqttService.is_connected());
    }
}

TEST_CASE("MQTT Service Connection and Publication")
{
    MqttService mqttService(
        "mqtts://mosquitto:8883",
        "ssl_publish_cpp",
        "lumac",
        "128Parsecs!",
        tls,
        1,
        std::chrono::seconds(10));

    SECTION("Connect and Publish to MQTT broker")
    {
        REQUIRE_NOTHROW(mqttService.connect().wait());
        REQUIRE(mqttService.is_connected());
        std::string topic = "test/topic";
        std::string payload = "Hello MQTT";
        REQUIRE_NOTHROW(mqttService.publish(topic, payload, 0, false).wait());
        REQUIRE_NOTHROW(mqttService.disconnect().wait());
        REQUIRE_FALSE(mqttService.is_connected());
    }
}

TEST_CASE("MQTT Service Connection and Subscription")
{
    MqttService mqttService(
        "mqtts://mosquitto:8883",
        "ssl_publish_cpp",
        "lumac",
        "128Parsecs!",
        tls,
        1,
        std::chrono::seconds(10));

    bool isMessageReceived = false;
    MessageHandler handler = [&isMessageReceived](mqtt::const_message_ptr msg)
    {
        isMessageReceived = true;
    };

    SECTION("Connect and Subscribe to MQTT broker")
    {
        REQUIRE_NOTHROW(mqttService.connect().wait());
        REQUIRE(mqttService.is_connected());
        std::string topic = "test/topic";
        REQUIRE_NOTHROW(mqttService.subscribe(topic, handler, 0).wait());
        std::this_thread::sleep_for(std::chrono::seconds(5));
        REQUIRE(isMessageReceived);
        REQUIRE_NOTHROW(mqttService.disconnect().wait());
        REQUIRE_FALSE(mqttService.is_connected());
    }
}
