#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "mqtt_service.hpp"

TlsConfig tls = {
    .ca_cert = "./../../config/mosquitto/certs/clients/node-1/ca.crt",
    .client_cert = "./../../config/mosquitto/certs/clients/node-1/node-1.crt",
    .client_key = "./../../config/mosquitto/certs/clients/node-1/node-1.pem",
    .verify_server = true};

struct mqttFixture
{
    MqttService mqtt;

    mqttFixture()
        : mqtt("mqtts://localhost:8883",
               "ssl_publish_cpp",
               "lumac",
               "128Parsecs!",
               tls,
               1,
               std::chrono::seconds(10))
    {
        mqtt.connect()->wait_for(mqtt.default_timeout_);
    }

    ~mqttFixture()
    {
        mqtt.disconnect()->wait_for(mqtt.default_timeout_);
    }
};

TEST_CASE_METHOD(mqttFixture, "MQTT Service Connection and Disconnection")
{
    SECTION("Connect and Disconnect")
    {
        REQUIRE(mqtt.is_connected());
    }
}

TEST_CASE_METHOD(mqttFixture, "MQTT Service Repeated Publishing")
{
    REQUIRE(mqtt.is_connected());
    int count = GENERATE(1, 3, 5);
    for (int i = 0; i < count; ++i)
    {
        mqtt.publish("test/topic", "Message #" + std::to_string(i))->wait_for(mqtt.default_timeout_);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
