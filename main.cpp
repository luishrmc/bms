#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "mqtt/async_client.h"

const std::string DFLT_SERVER_URI{"mqtts://localhost:8883"};
const std::string DFLT_CLIENT_ID{"ssl_publish_cpp"};

const std::string KEY_STORE{"node-0/node-0.pem"};
const std::string TRUST_STORE{"node-0/ca.crt"};

const std::string LWT_TOPIC{"events/disconnect"};
const std::string LWT_PAYLOAD{"Last will and testament."};

const int QOS = 1;
const auto TIMEOUT = std::chrono::seconds(10);

/////////////////////////////////////////////////////////////////////////////

/**
 * A callback class for use with the main MQTT client.
 */
class callback : public virtual mqtt::callback
{
public:
    void connection_lost(const std::string& cause) override
    {
        std::cout << "\nConnection lost" << std::endl;
        if (!cause.empty())
            std::cout << "\tcause: " << cause << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override
    {
        std::cout << "\tDelivery complete for token: " << (tok ? tok->get_message_id() : -1)
                  << std::endl;
    }
};

/////////////////////////////////////////////////////////////////////////////

using namespace std;

int main(void)
{
    {
        ifstream tstore(TRUST_STORE);
        if (!tstore) {
            cerr << "The trust store file does not exist: " << TRUST_STORE << endl;
            return 1;
        }

        ifstream kstore(KEY_STORE);
        if (!kstore) {
            cerr << "The key store file does not exist: " << KEY_STORE << endl;
            return 1;
        }
    }

    cout << "Initializing for server '" << DFLT_SERVER_URI << "'..." << endl;
    mqtt::async_client client(DFLT_SERVER_URI, DFLT_CLIENT_ID);

    callback cb;
    client.set_callback(cb);

    // Build the connect options, including SSL and a LWT message.

    auto sslopts = mqtt::ssl_options_builder()
                       .trust_store(TRUST_STORE)
                       .key_store(KEY_STORE)
                       .error_handler([](const std::string& msg) {
                           std::cerr << "SSL Error: " << msg << std::endl;
                       })
                       .finalize();

    auto willmsg = mqtt::message(LWT_TOPIC, LWT_PAYLOAD, QOS, true);

    auto connopts = mqtt::connect_options_builder()
                        .user_name("lumac")
                        .password("128Parsecs!")
                        .will(std::move(willmsg))
                        .ssl(std::move(sslopts))
                        .finalize();

    cout << "  ...OK" << endl;

    try {
        // Connect using SSL/TLS

        cout << "\nConnecting..." << endl;
        mqtt::token_ptr conntok = client.connect(connopts);
        cout << "Waiting for the connection..." << endl;
        conntok->wait();
        cout << "  ...OK" << endl;

        // Send a message

        cout << "\nSending message..." << endl;
        auto msg = mqtt::make_message("hello", "Hello secure C++ world!", QOS, false);
        client.publish(msg)->wait_for(TIMEOUT);
        cout << "  ...OK" << endl;

        // Disconnect

        cout << "\nDisconnecting..." << endl;
        client.disconnect()->wait();
        cout << "  ...OK" << endl;
    }
    catch (const mqtt::exception& exc) {
        cerr << exc.what() << endl;
        return 1;
    }

    return 0;
}

