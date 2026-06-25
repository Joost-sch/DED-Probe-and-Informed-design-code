#include "OOCSI.h"
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>

// Include your public key (generated with bin_signing.py)
#include "public_key.h"

// ---- WiFi & OOCSI ----
const char* ssid       = "OneplusThijs";
const char* password   = "heot2520";
const char* OOCSIName  = "DeD_Data_Handler_Trash_Bin_##";
const char* hostserver = "oocsi.id.tue.nl";

OOCSI oocsi = OOCSI();

// ---- OTA Configuration ----
const char* ota_password = nullptr;  // Set to "yourpassword" to enable, or nullptr to disable

// Hash & signature algorithm (must match bin_signing.py settings)
#define USE_SHA256  // Default, recommended
#define USE_RSA     // Recommended (works with rsa-2048, rsa-3072, rsa-4096)

uint32_t last_ota_time = 0;

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
const float    DROP_THRESHOLD  = 3000.0;
const uint32_t TARE_DELAY_MS   = 10000;

// ---- Timing ----
const uint32_t SEND_INTERVAL_MS = 1UL * 60UL * 1000UL;  // 1 minute
uint32_t       lastSendTime     = 0;

// ---- Auto-tare state ----
float    lastTotalWeight  = 0;
bool     dropDetected     = false;
uint32_t dropDetectedTime = 0;

// =====================================================================
// Scale functions
// =====================================================================

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
// OOCSI SEND #1 — Weight data, sent every 1 minute
// Fields: scale1, scale2, scale3, scale4, scaleTotal (all floats, grams)
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
// OOCSI SEND #3 — Trash removed event, sent once when auto-tare fires
// Fields: trashRemoved (int, always 1 when sent)
// =====================================================================
void sendTrashRemovedEvent() {
  oocsi.newMessage("DeD_Trash_Bin");
  oocsi.addInt("trashRemoved", 1);
  oocsi.sendMessage();
  Serial.println("[OOCSI] Trash removed event sent.");
}

// =====================================================================
// OTA setup
// =====================================================================
void setupOTA() {
  // Select hash algorithm
#ifdef USE_SHA256
  int hashType = HASH_SHA256;
  Serial.println("[OTA] Using SHA-256 hash");
#elif defined(USE_SHA384)
  int hashType = HASH_SHA384;
  Serial.println("[OTA] Using SHA-384 hash");
#elif defined(USE_SHA512)
  int hashType = HASH_SHA512;
  Serial.println("[OTA] Using SHA-512 hash");
#else
#error "Please define a hash algorithm (USE_SHA256, USE_SHA384, or USE_SHA512)"
#endif

  // Create verifier object
#ifdef USE_RSA
  static UpdaterRSAVerifier sign(PUBLIC_KEY, PUBLIC_KEY_LEN, hashType);
  Serial.println("[OTA] Using RSA signature verification");
#elif defined(USE_ECDSA)
  static UpdaterECDSAVerifier sign(PUBLIC_KEY, PUBLIC_KEY_LEN, hashType);
  Serial.println("[OTA] Using ECDSA signature verification");
#else
#error "Please define a signature type (USE_RSA or USE_ECDSA)"
#endif

  // Install signature verification BEFORE ArduinoOTA.begin()
  ArduinoOTA.setSignature(&sign);
  Serial.println("[OTA] Signature verification enabled");

  // Optional: Set OTA password (in addition to signature verification)
  if (ota_password != nullptr) {
    ArduinoOTA.setPassword(ota_password);
    Serial.println("[OTA] Password protection enabled");
  }

  // Configure OTA callbacks
  ArduinoOTA
    .onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("[OTA] Update starting: " + type);
      Serial.println("[OTA] Signature will be verified!");
    })
    .onEnd([]() {
      Serial.println("[OTA] Update complete! Signature verified. Rebooting...");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      if (millis() - last_ota_time > 500) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
        last_ota_time = millis();
      }
    })
    .onError([](ota_error_t error) {
      Serial.printf("[OTA] Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR)         Serial.println("Authentication Failed");
      else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin Failed (signature setup / no space / invalid partition)");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR)     Serial.println("End Failed (signature mismatch / firmware corrupted / MD5 error)");
    });

  ArduinoOTA.begin();

  Serial.println("[OTA] Server ready");
  Serial.printf("[OTA] Hostname: %s.local\n", ArduinoOTA.getHostname().c_str());
  Serial.printf("[OTA] IP: %s  Port: 3232\n", WiFi.localIP().toString().c_str());
  Serial.println("[OTA] Only signed firmware will be accepted!");
}

// =====================================================================
// Setup
// =====================================================================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(LED_BUILTIN, OUTPUT);
  oocsi.setActivityLEDPin(LED_BUILTIN);

  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    buttonLastState[i] = HIGH;
    buttonEvent[i]     = 0;
  }

  Serial.println("4x Mini Scale + OOCSI + Signed OTA");
  Serial.println("-------------------");

  // ---- Scan for scales ----
  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(SCALE_ADDRS[i]);
    byte error = Wire.endTransmission();
    Serial.print("Scale ");
    Serial.print(i + 1);
    if (error == 0) {
      Serial.print(" found at 0x");
      Serial.println(SCALE_ADDRS[i], HEX);
    } else {
      Serial.print(" NOT found at 0x");
      Serial.println(SCALE_ADDRS[i], HEX);
    }
  }
  Serial.println("-------------------");
  delay(1000);

  setupFilters();
  tareAllScales();

  // ---- WiFi (OOCSI handles the connection, OTA uses the same connection) ----
  oocsi.connect(OOCSIName, hostserver, ssid, password);
  Serial.println("Subscribing to DeD_Trash_Bin");
  oocsi.subscribe("DeD_Trash_Bin");

  // ---- OTA (WiFi must be up before this) ----
  setupOTA();

  lastSendTime = millis();
}

// =====================================================================
// Loop
// =====================================================================
void loop() {
  // ---- Handle OTA ----
  ArduinoOTA.handle();

  // ---- Read all scales ----
  float weight[4];
  float weightSum = 0;

  for (int i = 0; i < 4; i++) {
    weight[i]  = readWeight(SCALE_ADDRS[i]);
    weightSum += weight[i];
  }

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

  if (anyButtonPressed) {
    sendButtonData();                           // <-- OOCSI SEND #2
    sendWeightData(weight, weightSum);          // <-- OOCSI SEND #1 (also on button press)
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
        Serial.println("Further movement detected - resetting countdown.");
        dropDetectedTime = millis();
      }
    }
  }

  if (!dropDetected) {
    lastTotalWeight = weightSum;
  }

  // ---- Send weight data every 1 minute ----
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
