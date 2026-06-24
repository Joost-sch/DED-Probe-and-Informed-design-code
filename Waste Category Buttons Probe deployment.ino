/******************************************************************************
   4x M5 Mini Scale (SKU:U177) + OOCSI
   - Scale data sent every 5 minutes
   - Button presses sent immediately + weight snapshot at press moment
   - Weight from 5 seconds before and after button press also sent
   - Auto-tare after 500g drop + 10 second delay
   - Event sent to OOCSI when auto-tare fires
 ******************************************************************************/

#include "OOCSI.h"
#include <Wire.h>

// ---- WiFi & OOCSI ----
const char* ssid       = "OneplusThijs";
const char* password   = "heot2520";
const char* OOCSIName  = "DeD_Data_Handler_Trash_Bin_##";
const char* hostserver = "oocsi.id.tue.nl";

OOCSI oocsi = OOCSI();

// ---- Registers ----
#define WEIGHT_REG      0x10
#define OFFSET_REG      0x50
#define FILTER_LP_REG   0x80
#define FILTER_AVG_REG  0x81
#define FILTER_EMA_REG  0x82

// ---- Scales ----
const uint8_t SCALE_ADDRS[4] = {0x26, 0x27, 0x28, 0x29};

// ---- Buttons ----
const int BUTTON_PINS[]  = {17, 19, 18, 5};
const int BUTTON_COUNT   = 4;
bool      buttonLastState[BUTTON_COUNT];
int       buttonEvent[BUTTON_COUNT];

// ---- Auto-tare settings ----
const float    DROP_THRESHOLD  = 500.0;
const uint32_t TARE_DELAY_MS   = 10000;

// ---- Timing ----
const uint32_t SEND_INTERVAL_MS = 5UL * 60UL * 1000UL;  // 5 minutes
uint32_t       lastSendTime     = 0;

// ---- Auto-tare state ----
float    lastTotalWeight  = 0;
bool     dropDetected     = false;
uint32_t dropDetectedTime = 0;

// ---- Weight history for "before" snapshot (~5 seconds) ----
// Loop runs every 500ms, so 10 entries = 5 seconds of history
const int HISTORY_SIZE           = 10;
float     weightHistory[HISTORY_SIZE];
int       historyIndex           = 0;
bool      historyFull            = false;

// ---- "After" snapshot state ----
const uint32_t AFTER_DELAY_MS    = 5000;        // 5 seconds
bool           afterPending      = false;
uint32_t       afterTriggerTime  = 0;

// ---- Functions ----

float readWeight(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(WEIGHT_REG);
  Wire.endTransmission();

  Wire.requestFrom(addr, (uint8_t)4);

  if (Wire.available() == 4) {
    uint8_t bytes[4];
    for (int i = 0; i < 4; i++) bytes[i] = Wire.read();
    float weight;
    memcpy(&weight, bytes, 4);
    return weight;
  }
  return -1;
}

void tareAllScales() {
  Serial.println("Taring all scales...");
  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(OFFSET_REG);
    Wire.write(0x01);
    Wire.endTransmission();
    Serial.print("  Scale ");
    Serial.print(i + 1);
    Serial.println(" tared.");
  }
  delay(500);

  lastTotalWeight = 0;
  dropDetected    = false;

  // Clear history buffer after tare so old readings don't pollute new data
  historyFull  = false;
  historyIndex = 0;

  Serial.println("All scales tared!");
  Serial.println("-------------------");
}

void setupFilters() {
  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(FILTER_LP_REG);
    Wire.write(0x00);
    Wire.endTransmission();

    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(FILTER_AVG_REG);
    Wire.write(10);
    Wire.endTransmission();

    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(FILTER_EMA_REG);
    Wire.write(0x00);
    Wire.endTransmission();
  }
  Serial.println("Averaging filter set to 10 readings.");
}

// =====================================================================
// OOCSI SEND #1 — Weight data, sent every 5 minutes + on button press
// Fields: scale1, scale2, scale3, scale4, scaleTotal (floats, grams)
// =====================================================================
void sendWeightData(float weight[], float weightSum) {
  oocsi.newMessage("DeD_Trash_Bin");
  oocsi.addFloat("scale1",     weight[0]);
  oocsi.addFloat("scale2",     weight[1]);
  oocsi.addFloat("scale3",     weight[2]);
  oocsi.addFloat("scale4",     weight[3]);
  oocsi.addFloat("scaleTotal", weightSum);
  oocsi.sendMessage();
  Serial.println("[OOCSI] Weight data sent.");
}

// =====================================================================
// OOCSI SEND #2 — Button press, sent immediately on any button press
// Fields: button1, button2, button3, button4 (int, 0=idle, 1=pressed)
// =====================================================================
void sendButtonData() {
  oocsi.newMessage("DeD_Trash_Bin");
  oocsi.addInt("button1", buttonEvent[0]);
  oocsi.addInt("button2", buttonEvent[1]);
  oocsi.addInt("button3", buttonEvent[2]);
  oocsi.addInt("button4", buttonEvent[3]);
  oocsi.sendMessage();
  Serial.println("[OOCSI] Button data sent.");
}

// =====================================================================
// OOCSI SEND #3 — Trash removed event, sent when auto-tare fires
// Fields: trashRemoved (int, always 1 when sent)
// =====================================================================
void sendTrashRemovedEvent() {
  oocsi.newMessage("DeD_Trash_Bin");
  oocsi.addInt("trashRemoved", 1);
  oocsi.sendMessage();
  Serial.println("[OOCSI] Trash removed event sent.");
}

// =====================================================================
// OOCSI SEND #4 — Weight ~5 seconds BEFORE button press
// Fields: weightBefore (float, grams)
// =====================================================================
void sendWeightBefore() {
  float beforeWeight = weightHistory[historyIndex];  // oldest entry in buffer
  oocsi.newMessage("DeD_Trash_Bin");
  oocsi.addFloat("weightBefore", beforeWeight);
  oocsi.sendMessage();
  Serial.println("[OOCSI] Weight before button press sent.");
}

// =====================================================================
// OOCSI SEND #5 — Weight ~5 seconds AFTER button press
// Fields: weightAfter (float, grams)
// =====================================================================
void sendWeightAfter(float weightSum) {
  oocsi.newMessage("DeD_Trash_Bin");
  oocsi.addFloat("weightAfter", weightSum);
  oocsi.sendMessage();
  Serial.println("[OOCSI] Weight after button press sent.");
}

void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(LED_BUILTIN, OUTPUT);
  oocsi.setActivityLEDPin(LED_BUILTIN);

  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    buttonLastState[i] = HIGH;
    buttonEvent[i]     = 0;
  }

  // ---- Connect to WiFi & OOCSI first ----
  oocsi.connect(OOCSIName, hostserver, ssid, password);
  Serial.println("Subscribing to DeD_Trash_Bin");
  oocsi.subscribe("DeD_Trash_Bin");

  // ---- Then initialize scales ----
  Serial.println("4x Mini Scale + OOCSI");
  Serial.println("-------------------");

  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(SCALE_ADDRS[i]);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("Scale ");
      Serial.print(i + 1);
      Serial.print(" found at 0x");
      Serial.println(SCALE_ADDRS[i], HEX);
    } else {
      Serial.print("Scale ");
      Serial.print(i + 1);
      Serial.print(" NOT found at 0x");
      Serial.println(SCALE_ADDRS[i], HEX);
    }
  }

  Serial.println("-------------------");
  delay(1000);

  setupFilters();
  tareAllScales();

  lastSendTime = millis();
}

void loop() {
  // ---- Buttons — send immediately on press ----
  bool anyButtonPressed = false;
  for (int i = 0; i < BUTTON_COUNT; i++) {
    bool currentState = digitalRead(BUTTON_PINS[i]);
    if (buttonLastState[i] == HIGH && currentState == LOW) {
      buttonEvent[i]   = 1;
      anyButtonPressed = true;
      Serial.print("Button ");
      Serial.print(i + 1);
      Serial.println(" pressed!");
    } else {
      buttonEvent[i] = 0;
    }
    buttonLastState[i] = currentState;
  }

  // ---- Read all scales ----
  float weight[4];
  float weightSum = 0;

  for (int i = 0; i < 4; i++) {
    weight[i]  = readWeight(SCALE_ADDRS[i]);
    weightSum += weight[i];
  }

  // ---- Update weight history buffer ----
  weightHistory[historyIndex] = weightSum;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyIndex == 0) historyFull = true;

  if (anyButtonPressed) {
    sendButtonData();                           // <-- OOCSI SEND #2
    sendWeightData(weight, weightSum);          // <-- OOCSI SEND #1 (on button press)

    if (historyFull) {
      sendWeightBefore();                       // <-- OOCSI SEND #4
    } else {
      Serial.println("History not full yet, skipping weightBefore.");
    }

    // Arm the after timer
    afterPending     = true;
    afterTriggerTime = millis();
  }

  // ---- Send weight 5 seconds after button press ----
  if (afterPending && millis() - afterTriggerTime >= AFTER_DELAY_MS) {
    sendWeightAfter(weightSum);                 // <-- OOCSI SEND #5
    afterPending = false;
  }

  // ---- Auto-tare logic ----
  float weightDrop = lastTotalWeight - weightSum;

  if (!dropDetected && weightDrop > DROP_THRESHOLD) {
    dropDetected     = true;
    dropDetectedTime = millis();
    Serial.print("Big drop detected! (");
    Serial.print(weightDrop);
    Serial.println("g dropped) Waiting 10 seconds before taring...");
  }

  if (dropDetected) {
    uint32_t elapsed   = millis() - dropDetectedTime;
    uint32_t remaining = (TARE_DELAY_MS - elapsed) / 1000;

    if (elapsed >= TARE_DELAY_MS) {
      Serial.println("Auto-tare triggered after trash removal.");
      sendTrashRemovedEvent();                  // <-- OOCSI SEND #3
      tareAllScales();
    } else {
      static uint32_t lastPrintTime = 0;
      if (millis() - lastPrintTime >= 1000) {
        Serial.print("Auto-tare in ");
        Serial.print(remaining + 1);
        Serial.println(" seconds...");
        lastPrintTime = millis();
      }

      if (weightDrop > lastTotalWeight - weightSum + 50) {
        Serial.println("Further movement detected — resetting countdown.");
        dropDetectedTime = millis();
      }
    }
  }

  if (!dropDetected) {
    lastTotalWeight = weightSum;
  }

  // ---- Send weight data every 5 minutes ----
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    sendWeightData(weight, weightSum);          // <-- OOCSI SEND #1
    lastSendTime = millis();
  }

  // ---- Print readings to Serial ----
  Serial.print("Scale 1: "); Serial.print(weight[0]); Serial.print("g  |  ");
  Serial.print("Scale 2: "); Serial.print(weight[1]); Serial.print("g  |  ");
  Serial.print("Scale 3: "); Serial.print(weight[2]); Serial.print("g  |  ");
  Serial.print("Scale 4: "); Serial.print(weight[3]); Serial.print("g  |  ");
  Serial.print("Total: ");   Serial.print(weightSum); Serial.println("g");

  oocsi.keepAlive();
  delay(500);
}
