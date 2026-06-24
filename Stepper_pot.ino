// 28BYJ-48 Stepper Motor with ULN2003 — Potentiometer Control
// Arduino Uno/Nano — Half-Step Drive
// Pot position maps to a target step position; motor follows.

// Pin definitions
const int IN1 = 8;
const int IN2 = 9;
const int IN3 = 10;
const int IN4 = 11;
const int POT_PIN = A3;

// Range config
const int STEP_MIN = 0;
const int STEP_MAX = 4096;   // e.g. 2048 = half revolution, 1024 = quarter, etc.

const int STEP_DELAY_MS = 2;  // Speed — increase to slow down

// Half-step sequence
const int HALF_STEP[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

int currentStep = 0;   // Actual motor position (in steps)
int targetStep  = 0;   // Desired position from pot

// Write current step to coils
void applyStep() {
  int s = ((currentStep % 8) + 8) % 8;  // Always positive modulo
  digitalWrite(IN1, HALF_STEP[s][0]);
  digitalWrite(IN2, HALF_STEP[s][1]);
  digitalWrite(IN3, HALF_STEP[s][2]);
  digitalWrite(IN4, HALF_STEP[s][3]);
}

// Move one step toward target
// Returns true if a step was taken
bool stepTowardTarget() {
  if (currentStep == targetStep) return false;

  if (currentStep < targetStep) currentStep++;
  else                          currentStep--;

  applyStep();
  delay(STEP_DELAY_MS);
  return true;
}

// Release coils
void releaseMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// Read pot and map to step range
int readTargetStep() {
  int raw = analogRead(POT_PIN);  // 0–1023
  return map(raw, 0, 1023, STEP_MIN, STEP_MAX);
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  Serial.begin(9600);
  Serial.println("Pot stepper ready.");
}

void loop() {
  targetStep = readTargetStep();

  if (stepTowardTarget()) {
  } else {
    // Motor reached target — release coils to save power
    releaseMotor();

    // Re-read pot after short pause to catch new position
    delay(20);
  }
}
