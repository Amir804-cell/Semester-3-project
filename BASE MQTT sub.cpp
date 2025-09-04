* 
A C++ MQTT Subscriber that subscribes 
to the test/topic topic 
*/

#include <mosquittopp.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

class MQTTSubscriber : public mosqpp::mosquittopp
{
public:
    MQTTSubscriber(const char *id, const char *host, int port) : mosquittopp(id)
    {
        connect(host, port, 60);
    }

    void on_connect(int rc)
