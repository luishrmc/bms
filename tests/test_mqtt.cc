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
        "mqtts://localhost:8883",
        "ssl_publish_cpp",
        "lumac",
        "128Parsecs!",
        tls,
        1,
        std::chrono::seconds(10));

    SECTION("Connect + Publish + Subscribe + Disconnect")
    {
        mqtt::token_ptr tok = mqttService.connect();
        REQUIRE(tok->wait_for(mqttService.default_timeout_));
        REQUIRE(tok->get_return_code() == MQTTASYNC_SUCCESS);
        REQUIRE(mqttService.is_connected());

        mqtt::delivery_token_ptr pubTok = mqttService.publish("demo/topic", "hello", false);
        REQUIRE(pubTok->wait_for(mqttService.default_timeout_));
        REQUIRE(pubTok->get_return_code() == MQTTASYNC_SUCCESS);

        auto subTok = mqttService.subscribe("demo/topic", [](auto) { /* handler */ });
        REQUIRE(subTok->wait_for(mqttService.default_timeout_));
        REQUIRE(subTok->get_return_code() == MQTTASYNC_SUCCESS);

        auto unsubTok = mqttService.unsubscribe("demo/topic");
        REQUIRE(unsubTok->wait_for(mqttService.default_timeout_));
        REQUIRE(unsubTok->get_return_code() == MQTTASYNC_SUCCESS);

        REQUIRE(mqttService.disconnect()->wait_for(mqttService.default_timeout_));
    }
}
