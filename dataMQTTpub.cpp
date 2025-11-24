/*
v6 af ModbusMaster programmet til DV10 Ventilationsanlæg
NU MED SPARKPLUG B MQTT SUPPORT!

Programmet rapportere 'RunMode', 'Heat Exchange Efficiency', 'Runtime', temperatur, tryk, 
flow og sender alt data via MQTT i Sparkplug B format.

Kommandoer:
0 = Sluk ventilation
1 = Manuel reduceret hastighed
2 = Manuel normal hastighed
3 = Auto hastighed
r = Læs alle sensorer
m = Vis menu
a = Slå auto-read TIL/FRA
i = Ændre i auto-read intervallet (5-300 sekunder)
*/

#include <ModbusMaster.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Function Prototypes
void printMenu();
void setupWiFi();
void reconnectMQTT();
void publishSparkplugData();

// ================ WIFI & MQTT CONFIGURATION ================
const char* ssid = "DIT_WIFI_NAVN";
const char* password = "DIT_WIFI_PASSWORD";
const char* mqtt_server = "192.168.1.100";  // Din MQTT broker IP
const int mqtt_port = 1883;
const char* mqtt_user = "";          // Tom hvis ingen authentication
const char* mqtt_password = "";      // Tom hvis ingen authentication

// Sparkplug B Topic struktur
const char* group_id = "Ventilation";
const char* edge_node_id = "DV10_ESP32";
const char* device_id = "Sensor_Unit";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================ MODBUS COMMUNICATION CONFIGURATION ================
#define RX_PIN 36           // UART2 RX pin
#define TX_PIN 4            // UART2 TX pin
#define MAX485_DE 5         // RS485 Driver Enable pin
#define MAX485_RE_NEG 14    // RS485 Receiver Enable pin (active low)
#define BAUD_RATE 9600      // Communication speed
#define MODBUS_SLAVE_ID 1   // Slave device address

// ================ DATA BUFFERS ================
uint16_t holdingRegs[2];
uint16_t inputRegs[20];

ModbusMaster modbus;

// ================ AUTO-READ CONFIGURATION ================
bool autoReadEnabled = true;           // Auto-read on/off
unsigned long autoReadInterval = 5000; // Read every 5 seconds
unsigned long lastAutoRead = 0;        // Last auto-read timestamp

// ================ SPARKPLUG B DATATYPES ================
enum SparkplugDataType {
  INT16 = 3,
  INT32 = 4,
  INT64 = 5,
  UINT16 = 7,
  UINT32 = 8,
  UINT64 = 9,
  FLOAT = 10,
  DOUBLE = 11,
  BOOLEAN = 12,
  STRING = 13
};

// ================ SENSOR DATA STRUKTUR ================
struct SensorData {
  // System Status
  float heatExchangerEfficiency;
  uint16_t runMode;
  
  // Temperatures (°C)
  float outdoorTemp;
  float supplyAirTemp;
  float supplyAirSetpointTemp;
  float exhaustAirTemp;
  float extractAirTemp;
  
  // Pressures (Pa)
  float supplyAirPressure;
  float extractAirPressure;
  
  // Air Flows (m³/h)
  float supplyAirFlow;
  float extractAirFlow;
  float extraSupplyAirFlow;
  float extraExtractAirFlow;
  
  // Runtime (minutes)
  uint16_t supplyFanRuntime;
  uint16_t extractFanRuntime;
  
  // Timestamps
  unsigned long timestamp;
  int successfulReads;
  bool dataValid;
};

SensorData currentData = {0};

// ================ REGISTER DEFINITIONS ================
struct TempRegister {
  uint16_t address;
  const char* name;
};

TempRegister tempRegisters[] = {
  {0,  "Outdoor Temp"},
  {6,  "Supply Air Temp"},
  {7,  "Supply Air Setpoint Temp"},
  {8,  "Exhaust Air Temp"},
  {19, "Extract Air Temp"},
};

const int NUM_TEMPS = sizeof(tempRegisters) / sizeof(tempRegisters[0]);

struct PressureRegister {
  uint16_t address;
  const char* name;
};

PressureRegister pressureRegisters[] = {
  {12, "Supply Air Pressure"},
  {13, "Extract Air Pressure"},
};

const int NUM_PRESSURES = sizeof(pressureRegisters) / sizeof(pressureRegisters[0]);

struct FlowRegister {
  uint16_t address;
  const char* name;
};

FlowRegister flowRegisters[] = {
  {14, "Supply Air Flow"},
  {15, "Extract Air Flow"},
  {292, "Extra Supply Air Flow"},
  {293, "Extra Extract Air Flow"},
};

const int NUM_FLOWS = sizeof(flowRegisters) / sizeof(flowRegisters[0]);

struct RuntimeRegister {
  uint16_t address;
  const char* name;
};

RuntimeRegister runtimeRegisters[] = {
  {3, "Supply Air Fan Runtime"},
  {4, "Extract Air Fan Runtime"},
};

const int NUM_RUNTIMES = sizeof(runtimeRegisters) / sizeof(runtimeRegisters[0]);

// ================ RS485 Direction Control ================
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);
}

// ================ WIFI SETUP ================
void setupWiFi() {
  Serial.print("\n[WiFi] Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected");
    Serial.print("  IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi connection failed!");
  }
}

// ================ SPARKPLUG B: NODE BIRTH ================
void sendNodeBirth() {
  String topic = String("spBv1.0/") + group_id + "/NBIRTH/" + edge_node_id;
  
  StaticJsonDocument<512> doc;
  doc["timestamp"] = millis();
  doc["seq"] = 0;
  
  JsonArray metrics = doc.createNestedArray("metrics");
  
  JsonObject rebirth = metrics.createNestedObject();
  rebirth["name"] = "Node Control/Rebirth";
  rebirth["timestamp"] = millis();
  rebirth["dataType"] = BOOLEAN;
  rebirth["value"] = false;
  
  JsonObject bdSeq = metrics.createNestedObject();
  bdSeq["name"] = "bdSeq";
  bdSeq["timestamp"] = millis();
  bdSeq["dataType"] = INT64;
  bdSeq["value"] = 0;
  
  String payload;
  serializeJson(doc, payload);
  
  mqttClient.publish(topic.c_str(), payload.c_str());
  Serial.println("[MQTT] ✓ Node Birth (NBIRTH) sent");
}

// ================ SPARKPLUG B: DEVICE BIRTH ================
void sendDeviceBirth() {
  String topic = String("spBv1.0/") + group_id + "/DBIRTH/" + edge_node_id + "/" + device_id;
  
  DynamicJsonDocument doc(2048);
  doc["timestamp"] = millis();
  doc["seq"] = 1;
  
  JsonArray metrics = doc.createNestedArray("metrics");
  
  const char* metricNames[] = {
    "HeatExchangerEfficiency", "RunMode",
    "OutdoorTemp", "SupplyAirTemp", "SupplyAirSetpointTemp", "ExhaustAirTemp", "ExtractAirTemp",
    "SupplyAirPressure", "ExtractAirPressure",
    "SupplyAirFlow", "ExtractAirFlow", "ExtraSupplyAirFlow", "ExtraExtractAirFlow",
    "SupplyFanRuntime", "ExtractFanRuntime"
  };
  
  const char* units[] = {
    "%", "", "°C", "°C", "°C", "°C", "°C", "Pa", "Pa", 
    "m³/h", "m³/h", "m³/h", "m³/h", "min", "min"
  };
  
  SparkplugDataType dataTypes[] = {
    FLOAT, UINT16, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT,
    FLOAT, FLOAT, FLOAT, FLOAT, UINT16, UINT16
  };
  
  for (int i = 0; i < 15; i++) {
    JsonObject metric = metrics.createNestedObject();
    metric["name"] = metricNames[i];
    metric["timestamp"] = millis();
    metric["dataType"] = dataTypes[i];
    
    JsonObject properties = metric.createNestedObject("properties");
    JsonObject engUnit = properties.createNestedObject("engUnit");
    engUnit["type"] = STRING;
    engUnit["value"] = units[i];
    
    metric["value"] = 0;
  }
  
  String payload;
  serializeJson(doc, payload);
  
  mqttClient.publish(topic.c_str(), payload.c_str());
  Serial.println("[MQTT] ✓ Device Birth (DBIRTH) sent");
}

// ================ MQTT RECONNECT ================
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Attempting connection...");
    
    String clientId = "ESP32_DV10_" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("✓ Connected");
      sendNodeBirth();
      sendDeviceBirth();
    } else {
      Serial.print("✗ Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 5 sec");
      delay(5000);
    }
  }
}

// ================ HELPER: ADD METRIC (FLOAT) ================
void addMetric(JsonArray& metrics, const char* name, float value, SparkplugDataType dataType, unsigned long timestamp) {
  JsonObject metric = metrics.createNestedObject();
  metric["name"] = name;
  metric["timestamp"] = timestamp;
  metric["dataType"] = dataType;
  metric["value"] = value;
}

// ================ HELPER: ADD METRIC (UINT16) ================
void addMetric(JsonArray& metrics, const char* name, uint16_t value, SparkplugDataType dataType, unsigned long timestamp) {
  JsonObject metric = metrics.createNestedObject();
  metric["name"] = name;
  metric["timestamp"] = timestamp;
  metric["dataType"] = dataType;
  metric["value"] = value;
}

// ================ SPARKPLUG B: DATA PUBLISH ================
void publishSparkplugData() {
  if (!currentData.dataValid) {
    Serial.println("[MQTT] ✗ Data not valid, skipping publish");
    return;
  }
  
  String topic = String("spBv1.0/") + group_id + "/DDATA/" + edge_node_id + "/" + device_id;
  
  DynamicJsonDocument doc(2048);
  doc["timestamp"] = currentData.timestamp;
  static uint32_t seqNum = 2;
  doc["seq"] = seqNum++;
  
  JsonArray metrics = doc.createNestedArray("metrics");
  
  // System Status
  addMetric(metrics, "HeatExchangerEfficiency", currentData.heatExchangerEfficiency, FLOAT, currentData.timestamp);
  addMetric(metrics, "RunMode", currentData.runMode, UINT16, currentData.timestamp);
  
  // Temperatures
  addMetric(metrics, "OutdoorTemp", currentData.outdoorTemp, FLOAT, currentData.timestamp);
  addMetric(metrics, "SupplyAirTemp", currentData.supplyAirTemp, FLOAT, currentData.timestamp);
  addMetric(metrics, "SupplyAirSetpointTemp", currentData.supplyAirSetpointTemp, FLOAT, currentData.timestamp);
  addMetric(metrics, "ExhaustAirTemp", currentData.exhaustAirTemp, FLOAT, currentData.timestamp);
  addMetric(metrics, "ExtractAirTemp", currentData.extractAirTemp, FLOAT, currentData.timestamp);
  
  // Pressures
  addMetric(metrics, "SupplyAirPressure", currentData.supplyAirPressure, FLOAT, currentData.timestamp);
  addMetric(metrics, "ExtractAirPressure", currentData.extractAirPressure, FLOAT, currentData.timestamp);
  
  // Air Flows
  addMetric(metrics, "SupplyAirFlow", currentData.supplyAirFlow, FLOAT, currentData.timestamp);
  addMetric(metrics, "ExtractAirFlow", currentData.extractAirFlow, FLOAT, currentData.timestamp);
  addMetric(metrics, "ExtraSupplyAirFlow", currentData.extraSupplyAirFlow, FLOAT, currentData.timestamp);
  addMetric(metrics, "ExtraExtractAirFlow", currentData.extraExtractAirFlow, FLOAT, currentData.timestamp);
  
  // Runtime
  addMetric(metrics, "SupplyFanRuntime", currentData.supplyFanRuntime, UINT16, currentData.timestamp);
  addMetric(metrics, "ExtractFanRuntime", currentData.extractFanRuntime, UINT16, currentData.timestamp);
  
  String payload;
  serializeJson(doc, payload);
  
  bool success = mqttClient.publish(topic.c_str(), payload.c_str());
  
  if (success) {
    Serial.printf("[MQTT] ✓ Data published (%d bytes, %d metrics)\n", payload.length(), 15);
  } else {
    Serial.println("[MQTT] ✗ Publish failed");
  }
}

void setup() {
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial.println("\n===========================================");
  Serial.println("ESP32 Modbus RTU + MQTT Sparkplug B");
  Serial.println("===========================================\n");

  // Modbus Setup
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);
  Serial.println("✓ Modbus RTU Initialized\n");

  // WiFi & MQTT Setup
  setupWiFi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(2048);
  
  if (WiFi.status() == WL_CONNECTED) {
    reconnectMQTT();
  }

  printMenu();
}

// =============== CLI MENU ===============
void printMenu() {
  Serial.println("\n========== MENU ==========");
  Serial.println("Fan Mode Control:");
  Serial.println("  0 = Off");
  Serial.println("  1 = Manual Reduced");
  Serial.println("  2 = Manual Normal");
  Serial.println("  3 = Auto Speed");
  Serial.println("\nCommands:");
  Serial.println("  r = Read all sensors now");
  Serial.println("  a = Toggle auto-read ON/OFF");
  Serial.println("  i = Set auto-read interval");
  Serial.println("  m = Show menu");
  Serial.printf("\nAuto-read: %s (every %lu sec)\n", 
                autoReadEnabled ? "ON" : "OFF", 
                autoReadInterval / 1000);
  Serial.printf("WiFi: %s | MQTT: %s\n",
                WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected",
                mqttClient.connected() ? "Connected" : "Disconnected");
  Serial.println("==========================\n");
}

// =============== WRITE FAN MODE ===============
void writeFanMode(uint16_t mode) {
  if (mode > 3) {
    Serial.println("ERROR: Invalid fan mode. Use 0-3");
    return;
  }
  
  unsigned long startTime = millis();
  uint8_t result = modbus.writeSingleRegister(367, mode);
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    Serial.printf("✓ FanMode set to %u in %lums\n", mode, duration);
  } else {
    Serial.printf("✗ ERROR writing FanMode (code %u). Time=%lums\n", result, duration);
  }
}

// =============== READ EFFICIENCY ===============
bool readEfficiency() {
  uint8_t result = modbus.readInputRegisters(1, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float efficiency = rawValue / 10.0f;
    currentData.heatExchangerEfficiency = efficiency;
    Serial.printf("  %-25s [Reg   1]: %5u (%.1f %%)\n",
                  "Heat Exchanger Efficiency", rawValue, efficiency);
    return true;
  } else {
    Serial.printf("  %-25s [Reg   1]: ERROR (code %u)\n",
                  "Heat Exchanger Efficiency", result);
    return false;
  }
}

// =============== READ RUN MODE ===============
bool readRunMode() {
  uint8_t result = modbus.readInputRegisters(2, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    currentData.runMode = rawValue;
    
    const char* modeText;
    switch(rawValue) {
      case 0:  modeText = "Stopped"; break;
      case 1:  modeText = "Starting up"; break;
      case 2:  modeText = "Starting reduced speed"; break;
      case 3:  modeText = "Starting full speed"; break;
      case 4:  modeText = "Starting normal run"; break;
      case 5:  modeText = "Normal run"; break;
      case 6:  modeText = "Support control heating"; break;
      case 7:  modeText = "Support control cooling"; break;
      case 8:  modeText = "CO2 run"; break;
      case 9:  modeText = "Night cooling"; break;
      case 10: modeText = "Full speed stop"; break;
      case 11: modeText = "Stopping fan"; break;
      default: modeText = "Unknown mode"; break;
    }
    
    Serial.printf("  %-25s [Reg   2]: %5u (%s)\n",
                  "Run Mode", rawValue, modeText);
    return true;
  } else {
    Serial.printf("  %-25s [Reg   2]: ERROR (code %u)\n",
                  "Run Mode", result);
    return false;
  }
}

// =============== READ SINGLE TEMPERATURE ===============
bool readSingleTemp(uint16_t regAddress, const char* name, float* dataField) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float temperature = rawValue / 10.0f;
    *dataField = temperature;
    Serial.printf("  %-25s [Reg %3u]: %5u (%.1f °C)\n",
                  name, regAddress, rawValue, temperature);
    return true;
  } else {
    Serial.printf("  %-25s [Reg %3u]: ERROR (code %u)\n",
                  name, regAddress, result);
    return false;
  }
}

// =============== READ SINGLE PRESSURE ===============
bool readSinglePressure(uint16_t regAddress, const char* name, float* dataField) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float pressure = rawValue / 10.0f;
    *dataField = pressure;
    Serial.printf("  %-25s [Reg %3u]: %5u (%.1f Pa)\n",
                  name, regAddress, rawValue, pressure);
    return true;
  } else {
    Serial.printf("  %-25s [Reg %3u]: ERROR (code %u)\n",
                  name, regAddress, result);
    return false;
  }
}

// =============== READ SINGLE FLOW ===============
bool readSingleFlow(uint16_t regAddress, const char* name, float* dataField) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float flow = rawValue / 10.0f;
    *dataField = flow;
    Serial.printf("  %-25s [Reg %3u]: %5u (%.1f m³/h)\n",
                  name, regAddress, rawValue, flow);
    return true;
  } else {
    Serial.printf("  %-25s [Reg %3u]: ERROR (code %u)\n",
                  name, regAddress, result);
    return false;
  }
}

// =============== READ SINGLE RUNTIME ===============
bool readSingleRuntime(uint16_t regAddress, const char* name, uint16_t* dataField) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);

  if (result == modbus.ku8MBSuccess) {
    uint16_t raw = modbus.getResponseBuffer(0);
    *dataField = raw;
    Serial.printf("  %-25s [Reg %3u]: %5u (minutes)\n",
                  name, regAddress, raw);
    return true;
  } else {
    Serial.printf("  %-25s [Reg %3u]: ERROR (code %u)\n",
                  name, regAddress, result);
    return false;
  }
}

// =============== READ ALL SENSORS ===============
void readAllSensors() {
  unsigned long startTime = millis();
  int totalSuccess = 0;
  int totalSensors = 2 + NUM_TEMPS + NUM_PRESSURES + NUM_FLOWS + NUM_RUNTIMES;
  
  // Reset data structure
  currentData.timestamp = millis();
  currentData.successfulReads = 0;
  currentData.dataValid = false;
  
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║          READING ALL SENSORS                   ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
  
  // System Status
  Serial.println("--- System Status ---");
  if (readEfficiency()) totalSuccess++;
  delay(50);
  if (readRunMode()) totalSuccess++;
  delay(50);
  
  // Temperatures
  Serial.println("\n--- Temperatures ---");
  if (readSingleTemp(0, "Outdoor Temp", &currentData.outdoorTemp)) totalSuccess++;
  delay(50);
  if (readSingleTemp(6, "Supply Air Temp", &currentData.supplyAirTemp)) totalSuccess++;
  delay(50);
  if (readSingleTemp(7, "Supply Air Setpoint Temp", &currentData.supplyAirSetpointTemp)) totalSuccess++;
  delay(50);
  if (readSingleTemp(8, "Exhaust Air Temp", &currentData.exhaustAirTemp)) totalSuccess++;
  delay(50);
  if (readSingleTemp(19, "Extract Air Temp", &currentData.extractAirTemp)) totalSuccess++;
  delay(50);
  
  // Pressures
  Serial.println("\n--- Pressures ---");
  if (readSinglePressure(12, "Supply Air Pressure", &currentData.supplyAirPressure)) totalSuccess++;
  delay(50);
  if (readSinglePressure(13, "Extract Air Pressure", &currentData.extractAirPressure)) totalSuccess++;
  delay(50);
  
  // Air Flows
  Serial.println("\n--- Air Flows ---");
  if (readSingleFlow(14, "Supply Air Flow", &currentData.supplyAirFlow)) totalSuccess++;
  delay(50);
  if (readSingleFlow(15, "Extract Air Flow", &currentData.extractAirFlow)) totalSuccess++;
  delay(50);
  if (readSingleFlow(292, "Extra Supply Air Flow", &currentData.extraSupplyAirFlow)) totalSuccess++;
  delay(50);
  if (readSingleFlow(293, "Extra Extract Air Flow", &currentData.extraExtractAirFlow)) totalSuccess++;
  delay(50);
  
  // Runtime
  Serial.println("\n--- Runtime ---");
  if (readSingleRuntime(3, "Supply Air Fan Runtime", &currentData.supplyFanRuntime)) totalSuccess++;
  delay(50);
  if (readSingleRuntime(4, "Extract Air Fan Runtime", &currentData.extractFanRuntime)) totalSuccess++;
  delay(50);
  
  currentData.successfulReads = totalSuccess;
  currentData.dataValid = (totalSuccess > 0);
  
  unsigned long duration = millis() - startTime;
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.printf("║  Total: %d/%d successful reads in %lums         ║\n",
                totalSuccess, totalSensors, duration);
  Serial.println("╚════════════════════════════════════════════════╝\n");
}

// =============== HANDLE SERIAL INPUT ===============
void handleSerialInput() {
  if (Serial.available() > 0) {
    char input = Serial.read();
    
    while(Serial.available() > 0) {
      Serial.read();
    }
    
    switch(input) {
      case '0':
      case '1':
      case '2':
      case '3':
        writeFanMode(input - '0');
        break;
        
      case 'r':
      case 'R':
        readAllSensors();
        if (mqttClient.connected()) {
          publishSparkplugData();
        }
        break;
        
      case 'a':
      case 'A':
        autoReadEnabled = !autoReadEnabled;
        Serial.printf("Auto-read %s\n", autoReadEnabled ? "ENABLED" : "DISABLED");
        break;
        
      case 'i':
      case 'I':
        Serial.println("Enter interval in seconds (5-300):");
        while(Serial.available() == 0) {
          delay(10);
        }
        {
          int newInterval = Serial.parseInt();
          if (newInterval >= 5 && newInterval <= 300) {
            autoReadInterval = newInterval * 1000;
            Serial.printf("Auto-read interval set to %d seconds\n", newInterval);
          } else {
            Serial.println("Invalid interval. Use 5-300 seconds.");
          }
        }
        break;
        
      case 'm':
      case 'M':
        printMenu();
        break;
        
      case '\n':
      case '\r':
        break;
        
      default:
        Serial.println("Unknown command. Press 'm' for menu.");
        break;
    }
  }
}

// =============== LOOP ===============
void loop() {
  // Maintain MQTT connection
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    mqttClient.loop();
  }
  
  // Handle manual commands
  handleSerialInput();
  
  // Auto-read sensors if enabled
  if (autoReadEnabled) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastAutoRead >= autoReadInterval) {
      lastAutoRead = currentMillis;
      Serial.println("\n[AUTO-READ]");
      readAllSensors();
      
      // Publish via MQTT hvis forbundet
      if (mqttClient.connected()) {
        publishSparkplugData();
      }
    }
  }
  
  delay(10);
}
