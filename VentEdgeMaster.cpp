/*
v5 af ModbusMaster programmet til DV10 Ventilationsanlæg
Programmet rapportere 'RunMode', 'Heat Exchange Efficiency', 'Runtime', temperatur, tryk, 
flow og kan håndtere løbende bruger-input til at ændre RunMode mm.

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

// Function Prototype
void printMenu();

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

// Runtime struct
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

void setup() {
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial.println("\n===========================================");
  Serial.println("ESP32 Modbus RTU Communication");
  Serial.println("===========================================\n");

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.println("Modbus RTU Initialized Successfully\n");
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
bool readSingleTemp(uint16_t regAddress, const char* name) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float temperature = rawValue / 10.0f;
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
bool readSinglePressure(uint16_t regAddress, const char* name) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float pressure = rawValue / 10.0f;
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
bool readSingleFlow(uint16_t regAddress, const char* name) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);
  
  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);
    float flow = rawValue / 10.0f;
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
bool readSingleRuntime(uint16_t regAddress, const char* name) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);

  if (result == modbus.ku8MBSuccess) {
    uint16_t raw = modbus.getResponseBuffer(0);
    Serial.printf("  %-25s [Reg %3u]: %5u (minutes)\n",
                  name, regAddress, raw);
    return true;
  } else {
    Serial.printf("  %-25s [Reg %3u]: ERROR (code %u)\n",
                  name, regAddress, result);
    return false;
  }
}

// =============== READ ALARM SUMMARY ===============
bool readAlarmStatus(uint16_t regAddress, const char* name) {
  uint8_t result = modbus.readInputRegisters(regAddress, 1);

  if (result == modbus.ku8MBSuccess) {
    uint16_t rawValue = modbus.getResponseBuffer(0);

    if (regAddress == 183) {
      Serial.printf("  %-25s [Reg %3u]: %s\n", name, regAddress, (rawValue > 0 ? "Aktiv" : "Ingen"));
    } else {
      if (rawValue == 0) {
        Serial.printf("  %-25s [Reg %3u]: Ingen alarm\n", name, regAddress);
      } else {
        Serial.printf("  %-25s [Reg %3u]: Alarm aktiv (kode: %u)\n", name, regAddress, rawValue);
      }
    }
    return true;
  } else {
    Serial.printf("  %-25s [Reg %3u]: ERROR (code %u)\n", name, regAddress, result);
    return false;
  }
}


// =============== READ ALL SENSORS ===============
void readAllSensors() {
  unsigned long startTime = millis();
  int totalSuccess = 0;
  int totalSensors = 2 + NUM_TEMPS + NUM_PRESSURES + NUM_FLOWS + NUM_RUNTIMES; // Efficiency + RunMode + temps + pressures + flows
  
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║          READING ALL SENSORS                   ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
  
  // System Status
  Serial.println("--- System Status ---");
  if (readEfficiency()) totalSuccess++;
  delay(50);
  if (readRunMode()) totalSuccess++;
  delay(50);
  
  // Temperatures (værdi i C)
  Serial.println("\n--- Temperatures ---");
  for (int i = 0; i < NUM_TEMPS; i++) {
    if (readSingleTemp(tempRegisters[i].address, tempRegisters[i].name)) {
      totalSuccess++;
    }
    delay(50);
  }
  
  // Pressures (værdi i Pa)
  Serial.println("\n--- Pressures ---");
  for (int i = 0; i < NUM_PRESSURES; i++) {
    if (readSinglePressure(pressureRegisters[i].address, pressureRegisters[i].name)) {
      totalSuccess++;
    }
    delay(50);
  }
  
  // Air Flows (værdi i m3/h)
  Serial.println("\n--- Air Flows ---");
  for (int i = 0; i < NUM_FLOWS; i++) {
    if (readSingleFlow(flowRegisters[i].address, flowRegisters[i].name)) {
      totalSuccess++;
    }
    delay(50);
  }
  
    // Runtime (værdi i minutter)
  Serial.println("\n--- Runtime ---");
  for (int i = 0; i < NUM_RUNTIMES; i++) {
    if (readSingleRuntime(runtimeRegisters[i].address, runtimeRegisters[i].name)){
      totalSuccess++;
    }
    delay(50);
  } 


  
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
    
    // Clear any remaining characters in buffer
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
        // Ignore newlines
        break;
        
      default:
        Serial.println("Unknown command. Press 'm' for menu.");
        break;
    }
  }
}

// =============== LOOP ===============
void loop() {
  // Handle manual commands
  handleSerialInput();
  
  // Auto-read sensors if enabled
  if (autoReadEnabled) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastAutoRead >= autoReadInterval) {
      lastAutoRead = currentMillis;
      Serial.println("\n[AUTO-READ]");
      readAllSensors();
    }
  }
  
  delay(10);
}
