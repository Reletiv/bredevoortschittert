#include <EEPROM.h>

// Pin definitions
#define IN1 3
#define IN2 5
#define IN3 6
#define IN4 9
#define IN5 10
#define IN6 7
#define IN7 11
#define IN8 8
#define BUTTON 2

// Constants
const unsigned long debounceDelay = 50; // ms
const unsigned long flipIntervalMicros = 5560; // 180Hz
const byte maxMode = 5;

// Speed per mode (index 0=OFF, 1=SLOW, ..., 5=TEST)
const unsigned long patternSpeeds[] = {
  0,    // OFF
  5000, // SLOW
  2000, // NORMAL
  1000, // QUICK
  500,  // FAST
  0     // TEST (no sequence stepping)
};

// Fun step pattern: wave, bounce, pulse
const byte sequence[][4] = {
  // Build up
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {1, 1, 1, 0},
  {1, 1, 1, 1},
  {1, 1, 1, 0},
  {1, 1, 0, 0},
  {1, 0, 0, 0},
  {0, 0, 0, 0},

  // Bounce right
  {1, 0, 0, 0},
  {0, 1, 0, 0},
  {0, 0, 1, 0},
  {0, 0, 0, 1},
  {0, 0, 1, 0},
  {0, 1, 0, 0},
  {1, 0, 0, 0},
  {0, 0, 0, 0},

  // All on/off flash
  {1, 1, 1, 1},
  {0, 0, 0, 0},
  {1, 1, 1, 1},
  {0, 0, 0, 0},

  // Hold last light
  {0, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 0, 0, 1}, // extended hold
};

const int totalSteps = sizeof(sequence) / sizeof(sequence[0]);

// State
int mode = 0;
bool buttonPressed = false;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

unsigned long lastFlipTime = 0;
bool polarity = false;

unsigned long lastPatternTime = 0;
int currentStep = 0;
bool activeChannels[4] = {0, 0, 0, 0};

// Heartbeat LED variables
unsigned long lastHeartbeatTime = 0;
int heartbeatState = 0;

void setup() {
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(IN5, OUTPUT); pinMode(IN6, OUTPUT);
  pinMode(IN7, OUTPUT); pinMode(IN8, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  Serial.begin(9600);
  // while (!Serial);

  mode = EEPROM.read(0);
  if (mode > maxMode) mode = 0;

  Serial.print("Restored mode: ");
  Serial.println(mode);
  loadMode(mode);
}

void loop() {
  unsigned long nowMillis = millis();
  unsigned long nowMicros = micros();

  // ---- Button handling ----
  bool reading = digitalRead(BUTTON);
  if (reading != lastButtonState) {
    lastDebounceTime = nowMillis;
  }

  if ((nowMillis - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      mode = (mode + 1) % (maxMode + 1);  // cycle through 0–5
      EEPROM.write(0, mode);
      Serial.print("Mode changed to: ");
      Serial.println(mode);
      loadMode(mode);
    }
    if (reading == HIGH && buttonPressed) {
      buttonPressed = false;
    }
  }
  lastButtonState = reading;

  // ---- Polarity flipping (always running if mode != OFF) ----
  if (mode != 0 && nowMicros - lastFlipTime >= flipIntervalMicros) {
    lastFlipTime = nowMicros;
    polarity = !polarity;
    updatePolarity();
  }

  // ---- Pattern sequencing ----
  if (mode > 0 && mode < 5) {
    unsigned long interval = patternSpeeds[mode];
    if (nowMillis - lastPatternTime >= interval) {
      lastPatternTime = nowMillis;
      currentStep = (currentStep + 1) % totalSteps;
      loadStep(currentStep);
      Serial.print("Step ");
      Serial.println(currentStep);
    }
  }

  // ---- Heartbeat LED (status) ----
  if (mode != 0) {
    heartbeatLED(nowMillis);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    // Reset heartbeat state so it starts fresh on next mode change
    heartbeatState = 0;
    lastHeartbeatTime = nowMillis;
  }
}

void heartbeatLED(unsigned long now) {
  switch (heartbeatState) {
    case 0: // first pulse ON
      digitalWrite(LED_BUILTIN, HIGH);
      if (now - lastHeartbeatTime >= 100) { // pulse duration 100ms
        heartbeatState = 1;
        lastHeartbeatTime = now;
      }
      break;
    case 1: // first pulse OFF
      digitalWrite(LED_BUILTIN, LOW);
      if (now - lastHeartbeatTime >= 100) { // short pause 100ms
        heartbeatState = 2;
        lastHeartbeatTime = now;
      }
      break;
    case 2: // second pulse ON
      digitalWrite(LED_BUILTIN, HIGH);
      if (now - lastHeartbeatTime >= 150) { // second pulse 150ms
        heartbeatState = 3;
        lastHeartbeatTime = now;
      }
      break;
    case 3: // second pulse OFF (long pause)
      digitalWrite(LED_BUILTIN, LOW);
      if (now - lastHeartbeatTime >= 2000) { // long rest 700ms
        heartbeatState = 0;
        lastHeartbeatTime = now;
      }
      break;
  }
}

// ---- Load mode settings ----
void loadMode(int m) {
  currentStep = 0;
  if (m == 0) {
    clearActiveChannels();
    allOutputsLow();
  } else if (m == 5) {
    // TEST mode — all ON
    for (int i = 0; i < 4; i++) activeChannels[i] = true;
    updatePolarity();
  } else {
    loadStep(currentStep);
  }
}

// ---- Step loader ----
void loadStep(int step) {
  for (int i = 0; i < 4; i++) {
    activeChannels[i] = sequence[step][i];
  }
  updatePolarity();  // safe to call even with no change
}

// ---- Polarity update ----
void updatePolarity() {
  allOutputsLow();

  if (activeChannels[0]) {
    digitalWrite(IN1, polarity ? HIGH : LOW);
    digitalWrite(IN2, polarity ? LOW : HIGH);
  }
  if (activeChannels[1]) {
    digitalWrite(IN3, polarity ? HIGH : LOW);
    digitalWrite(IN4, polarity ? LOW : HIGH);
  }
  if (activeChannels[2]) {
    digitalWrite(IN5, polarity ? HIGH : LOW);
    digitalWrite(IN6, polarity ? LOW : HIGH);
  }
  if (activeChannels[3]) {
    digitalWrite(IN7, polarity ? HIGH : LOW);
    digitalWrite(IN8, polarity ? LOW : HIGH);
  }
}

// ---- Utility ----
void clearActiveChannels() {
  for (int i = 0; i < 4; i++) activeChannels[i] = false;
}

void allOutputsLow() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  digitalWrite(IN5, LOW); digitalWrite(IN6, LOW);
  digitalWrite(IN7, LOW); digitalWrite(IN8, LOW);
}
