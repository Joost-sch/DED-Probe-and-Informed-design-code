/******************************************************************************
   4x M5 Mini Scale (SKU:U177) - Automatic tare after trash removal
   - Button pin 17: manual tare all scales
   - Averaging filter only: last 10 readings
   - Auto-tare after 10 seconds when total weight drops > 500g
 ******************************************************************************/

#include <Wire.h>

// ---- Registers ----
#define WEIGHT_REG      0x10
#define OFFSET_REG      0x50
#define FILTER_LP_REG   0x80
#define FILTER_AVG_REG  0x81
#define FILTER_EMA_REG  0x82

// ---- Scales ----
const uint8_t SCALE_ADDRS[4] = {0x26, 0x27, 0x28, 0x29};

// ---- Buttons ----
const int BUTTON_TARE = 17;
bool lastStateTare = HIGH;

// ---- Auto-tare settings ----
const float  DROP_THRESHOLD   = 500.0;  // grams — drop bigger than this triggers auto-tare
const uint32_t TARE_DELAY_MS  = 10000;  // ms to wait after drop before taring (10 seconds)

// ---- State ----
float    lastTotalWeight   = 0;
bool     dropDetected      = false;
uint32_t dropDetectedTime  = 0;

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

  // Reset state after tare
  lastTotalWeight = 0;
  dropDetected    = false;

  Serial.println("All scales tared!");
  Serial.println("-------------------");
}

void setupFilters() {
  for (int i = 0; i < 4; i++) {
    // Disable Low Pass filter
    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(FILTER_LP_REG);
    Wire.write(0x00);
    Wire.endTransmission();

    // Averaging filter: last 10 readings
    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(FILTER_AVG_REG);
    Wire.write(10);
    Wire.endTransmission();

    // Disable EMA filter
    Wire.beginTransmission(SCALE_ADDRS[i]);
    Wire.write(FILTER_EMA_REG);
    Wire.write(0x00);
    Wire.endTransmission();
  }
  Serial.println("Averaging filter set to 10 readings.");
}

void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(BUTTON_TARE, INPUT_PULLUP);

  Serial.println("4x Mini Scale - Auto tare on trash removal");
  Serial.println("-------------------------------------------");

  // Check all scales are found
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

  Serial.println("-------------------------------------------");
  delay(1000);

  setupFilters();
  tareAllScales();
}

void loop() {
  // ---- Manual tare button ----
  bool stateTare = digitalRead(BUTTON_TARE);
  if (lastStateTare == HIGH && stateTare == LOW) {
    Serial.println("Manual tare triggered.");
    dropDetected = false;  // Cancel any pending auto-tare
    tareAllScales();
  }
  lastStateTare = stateTare;

  // ---- Read all scales ----
  float weight[4];
  float weightSum = 0;

  for (int i = 0; i < 4; i++) {
    weight[i]  = readWeight(SCALE_ADDRS[i]);
    weightSum += weight[i];
  }

  // ---- Auto-tare logic ----
  float weightDrop = lastTotalWeight - weightSum;

  if (!dropDetected && weightDrop > DROP_THRESHOLD) {
    // Big drop detected — start countdown
    dropDetected     = true;
    dropDetectedTime = millis();
    Serial.print("Big drop detected! (");
    Serial.print(weightDrop);
    Serial.println("g dropped) Waiting 10 seconds before taring...");
  }

  if (dropDetected) {
    uint32_t elapsed = millis() - dropDetectedTime;
    uint32_t remaining = (TARE_DELAY_MS - elapsed) / 1000;

    if (elapsed >= TARE_DELAY_MS) {
      // Countdown finished — tare now
      Serial.println("Auto-tare triggered after trash removal.");
      tareAllScales();
    } else {
      // Still counting down — print remaining time every second
      static uint32_t lastPrintTime = 0;
      if (millis() - lastPrintTime >= 1000) {
        Serial.print("Auto-tare in ");
        Serial.print(remaining + 1);
        Serial.println(" seconds...");
        lastPrintTime = millis();
      }

      // If weight drops even further during countdown, reset the timer
      // (e.g. someone is still adjusting the bin)
      if (weightDrop > lastTotalWeight - weightSum + 50) {
        Serial.println("Further movement detected — resetting countdown.");
        dropDetectedTime = millis();
      }
    }
  }

  // Only update lastTotalWeight when no drop is pending
  // so we don't lose track of the reference point
  if (!dropDetected) {
    lastTotalWeight = weightSum;
  }

  // ---- Print readings ----
  Serial.print("Scale 1: "); Serial.print(weight[0]); Serial.print("g  |  ");
  Serial.print("Scale 2: "); Serial.print(weight[1]); Serial.print("g  |  ");
  Serial.print("Scale 3: "); Serial.print(weight[2]); Serial.print("g  |  ");
  Serial.print("Scale 4: "); Serial.print(weight[3]); Serial.print("g  |  ");
  Serial.print("Total: ");   Serial.print(weightSum); Serial.println("g");

  delay(500);
}