#include <mosquittopp.h>
#include <iostream>
#include <cstring>
#include <unistd.h>

class MQTTPublisher : public mosqpp::mosquittopp {
public:
    MQTTPublisher(const char *id, const char *host, int port) : mosqpp::mosquit>
        int keepalive = 60;
        connect(host, port, keepalive);
    }

    void on_connect(int rc) override {
        if (rc == 0) {
            std::cout << "Connected to MQTT broker successfully!" << std::endl;
        } else {
            std::cerr << "Failed to connect to MQTT broker. Return code: " << r>
        }
    }

