#include <ModbusMaster.h>

// ================ MODBUS COMMUNICATION CONFIGURATION ================
#define RX_PIN 36           // UART2 RX pin
#define TX_PIN 4            // UART2 TX pin
#define MAX485_DE 5         // RS485 Driver Enable pin
#define MAX485_RE_NEG 14    // RS485 Receiver Enable pin (active low)
#define BAUD_RATE 9600      // Communication speed
#define MODBUS_SLAVE_ID 1   // Slave device address

// ================ DATA BUFFERS ================
uint16_t holdingRegs[2];
uint16_t inputRegs[2];

ModbusMaster modbus;

// ================ RS485 Direction Control ================
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);  // Disable receiver
  digitalWrite(MAX485_DE, HIGH);      // Enable driver
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);   // Enable receiver
  digitalWrite(MAX485_DE, LOW);       // Disable driver
}

void setup() {
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial.println("ESP32 Modbus RTU Communication Initializing...");

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.println("Modbus RTU Communication Initialized Successfully");
}

// =============== WRITE FAN MODE ===============
// Fan Mode Control (Holding Register 367)
// 0 = Off
// 1 = Manual Reduced Speed
// 2 = Manual Normal Speed
// 3 = Auto Speed
void writeFanMode(uint16_t mode) {
  unsigned long startTime = millis();

  uint8_t result = modbus.writeSingleRegister(367, mode);
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    Serial.printf("SUCCESS: FanMode=%u written to Reg[367]. Time=%lums\n",
                  mode, duration);
  } else {
    Serial.printf("ERROR writing FanMode=%u to Reg[367] (code %u). Time=%lums\n",
                  mode, result, duration);
  }
}

// =============== READ INPUT REGISTERS ===============
void readInputRegisters() {
  unsigned long startTime = millis();
  uint8_t result = modbus.readInputRegisters(19, 1);
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);

    float temperature = inputRegs[0] / 10.0f;
    Serial.printf("Input Reg[19] = %u (Temperature: %.1f °C)  Time=%lums\n",
                  inputRegs[0], temperature, duration);
  } else {
    Serial.printf("Read Input Error (code %u). Time=%lums\n",
                  result, duration);
  }
}

// =============== LOOP ===============
void loop() {
  // Step 1: Set fanmode to reduced (1)
  writeFanMode(1);

  // Step 2: Allow system to react
  delay(300);

  // Step 3: Read temperature (reg 19)
  readInputRegisters();

  Serial.println("----------------------------");

  delay(2000);
}
