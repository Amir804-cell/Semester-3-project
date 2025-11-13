#include <gtest/gtest.h>
#include "../edge_device.h"

//  Connection test 1 
TEST(ConnectionTest, TypicalWiFiCase) {
    MockWiFi wifi;
    wifi.connected = true;
    EXPECT_EQ(connectWiFi(wifi, "ssid", "pass"), "WiFi connected");
}

TEST(ConnectionTest, WiFiNotInitialized) {
    MockWiFi wifi;
    wifi.initialized = false;
    EXPECT_EQ(connectWiFi(wifi, "ssid", "pass"), "WiFi init failed.");
}

//  Connection test 2 
TEST(ConnectionTest, TypicalMQTTCase) {
    MockMQTT client;
    client.connected = true;
    EXPECT_EQ(connectMQTT(client, true, "192.168.1.100"), "WiFi and MQTT connected.");
}

TEST(ConnectionTest, InvalidWiFiCredentials) {
    MockMQTT client;
    EXPECT_EQ(connectMQTT(client, false, "192.168.1.100"), "Invalid WiFi credentials.");
}

TEST(ConnectionTest, BrokerUnreachable) {
    MockMQTT client;
    client.connected = false;
    EXPECT_EQ(connectMQTT(client, true, "192.168.1.100"), "MQTT server unreachable.");
    EXPECT_TRUE(client.reconnect_called);
}

TEST(ConnectionTest, LostConnectionDuringRuntime) {
    MockMQTT client;
    client.connected = false;
    connectMQTT(client, true, "192.168.1.100");
    EXPECT_TRUE(client.reconnect_called);
}

//  Sensor reading test 
TEST(SensorTest, NominalValue) {
    EXPECT_EQ(readSensorValue(2500), "Sensor value accepted.");
}

TEST(SensorTest, MinEdgeValue) {
    EXPECT_EQ(readSensorValue(0), "Min edge case logged.");
}

TEST(SensorTest, MaxEdgeValue) {
    EXPECT_EQ(readSensorValue(4950), "Max edge case logged.");
}
