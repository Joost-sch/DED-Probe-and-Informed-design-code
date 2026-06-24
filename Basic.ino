/******************************************************************************
   Example of the OOCSI-ESP library connecting to WiFi and sending messages
   over OOCSI. Designed to work with the Processing OOCSI receiver example
   that is provided in the same directory
 ******************************************************************************/

#include "OOCSI.h"

const char* ssid = "JostieTostie";
const char* password = "LekkerEten123!";

const char* OOCSIName = "DeD_Data_Trash_Bin_##";
const char* hostserver = "oocsi.id.tue.nl";

OOCSI oocsi = OOCSI();

// --- Button configuration ---
const int BUTTON_PINS[] = {17, 19, 18, 5};  // Add or change pins here
const int BUTTON_COUNT = 4;

bool buttonLastState[BUTTON_COUNT];
int  buttonEvent[BUTTON_COUNT];

float floatMap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setup() {
  Serial.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);
  oocsi.setActivityLEDPin(LED_BUILTIN);

  // Initialize all buttons
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    buttonLastState[i] = HIGH;
    buttonEvent[i] = 0;
  }

  oocsi.connect(OOCSIName, hostserver, ssid, password);

  Serial.println("subscribing to DeD_Trash_Bin");
  oocsi.subscribe("DeD_Trash_Bin");
}

void loop() {
  // --- Read all buttons and detect press edges ---
  for (int i = 0; i < BUTTON_COUNT; i++) {
    bool currentState = digitalRead(BUTTON_PINS[i]);

    if (buttonLastState[i] == HIGH && currentState == LOW) {
      buttonEvent[i] = 1;
      Serial.print("Button ");
      Serial.print(i + 1);
      Serial.println(" pressed!");
    } else {
      buttonEvent[i] = 0;
    }
    buttonLastState[i] = currentState;
  }

  // --- Build and send OOCSI message ---
  oocsi.newMessage("DeD_Trash_Bin");

  int analogValue = analogRead(36);
  float voltage = floatMap(analogValue, 0, 4095, 0, 3.3);

  Serial.print("Analog: ");
  Serial.print(analogValue);
  Serial.print(", Voltage: ");
  Serial.println(voltage);

  oocsi.addInt("Analog", analogValue);
  oocsi.addFloat("Voltage", voltage);

  // Send all button events
  oocsi.addInt("button1", buttonEvent[0]);
  oocsi.addInt("button2", buttonEvent[1]);
  oocsi.addInt("button3", buttonEvent[2]);
  oocsi.addInt("button4", buttonEvent[3]);

  oocsi.sendMessage();

  oocsi.keepAlive();
  delay(500);
}