#include <Arduino.h>
#include <ModbusRTU.h>

// --------------------
// CONFIGURATION
// --------------------
#define SLAVE_ID        1                 // Change later to your vent's Modbus ID
#define BAUD_RATE       19200             // Try 19200 first (most HVAC use this)
#define SERIAL_MODE     SERIAL_8E1        // Even parity; try SERIAL_8N1 if needed
#define RS485_RX_PIN    16                // ESP32-PoE UART2 RX
#define RS485_TX_PIN    17                // ESP32-PoE UART2 TX
#define RS485_DE_RE_PIN 22                // DE/RE control pin on ESP32-PoE

// Placeholder Modbus addresses
#define COIL_ADDR       0
#define HREG_ADDR       0

// --------------------
// GLOBAL VARIABLES
// --------------------
ModbusRTU mb;
volatile bool busy = false;
uint8_t step = 0;                         // 0=read coil, 1=read hreg, 2=write coil, 3=write hreg
uint32_t lastMillis = 0;
bool gCoil = false;
uint16_t gHreg = 0;

// --------------------
// CALLBACKS
// --------------------
static void printRC(const char* what, Modbus::ResultCode rc) {
  Serial.printf("%s -> rc=0x%02X\n", what, rc); // 0x00 OK, 0xE2 timeout, 0xE4 bad frame, etc.
}

bool cbReadCoil(Modbus::ResultCode rc, uint16_t, void* data) {
  busy = false;
  if (rc == Modbus::EX_SUCCESS) {
    bool* v = (bool*)data;
    Serial.printf("Coil[%u] = %u\n", COIL_ADDR, *v);
  } else {
    printRC("readCoil", rc);
  }
  step = 1;
  return true;
}

bool cbReadHreg(Modbus::ResultCode rc, uint16_t, void* data) {
  busy = false;
  if (rc == Modbus::EX_SUCCESS) {
    uint16_t* v = (uint16_t*)data;
    Serial.printf("HReg[%u] = %u\n", HREG_ADDR, *v);
  } else {
    printRC("readHreg", rc);
  }
  step = 2;
  return true;
}

bool cbWrite(Modbus::ResultCode rc, uint16_t, void*) {
  busy = false;
  printRC("write", rc);
  step = (step + 1) % 4;
  return true;
}

// --------------------
// SETUP
// --------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESP32-PoE Modbus RTU master starting...");

  Serial2.begin(BAUD_RATE, SERIAL_MODE, RS485_RX_PIN, RS485_TX_PIN);
  pinMode(RS485_DE_RE_PIN, OUTPUT);

  mb.begin(&Serial2, RS485_DE_RE_PIN);
  mb.master();

  Serial.printf("Config: SLAVE_ID=%u, BAUD=%u, MODE=%s, DE/RE=GPIO%d\n",
                SLAVE_ID, BAUD_RATE,
                (SERIAL_MODE==SERIAL_8E1 ? "8E1" : "8N1"),
                RS485_DE_RE_PIN);
  Serial.println("Polling every 3s (coil → hreg → write coil → write hreg) ...");
}

// --------------------
// LOOP
// --------------------
void loop() {
  if ((millis() - lastMillis > 3000) && !busy) {
    lastMillis = millis();
    busy = true;

    switch (step) {
      case 0: // Read coil
        if (!mb.readCoil(SLAVE_ID, COIL_ADDR, &gCoil, 1, cbReadCoil)) {
          busy = false;
          Serial.println("Queue busy: readCoil not started");
        }
        break;

      case 1: // Read holding register
        if (!mb.readHreg(SLAVE_ID, HREG_ADDR, &gHreg, 1, cbReadHreg)) {
          busy = false;
          Serial.println("Queue busy: readHreg not started");
        }
        break;

      case 2: { // Write coil
        bool newCoil = !gCoil;
        if (!mb.writeCoil(SLAVE_ID, COIL_ADDR, newCoil, cbWrite)) {
          busy = false;
          Serial.println("Queue busy: writeCoil not started");
        } else {
          Serial.printf("Writing Coil[%u] = %u\n", COIL_ADDR, newCoil);
        }
        break;
      }

      case 3: { // Write holding register
        uint16_t newReg = random(0, 100);
        if (!mb.writeHreg(SLAVE_ID, HREG_ADDR, newReg, cbWrite)) {
          busy = false;
          Serial.println("Queue busy: writeHreg not started");
        } else {
          Serial.printf("Writing HReg[%u] = %u\n", HREG_ADDR, newReg);
        }
        break;
      }
    }
  }

  mb.task();
  yield();
}
