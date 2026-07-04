#include <M5Unified.h>
#include <FastLED.h>

const int RELAY_PIN = 2;

const int LED_PIN = 35;
const int NUM_LEDS = 1;
CRGB leds[NUM_LEDS];

bool latchMode = false;
bool relayState = false;
bool lastPressed = false;

void setLed(CRGB color) {
  leds[0] = color;
  FastLED.show();
}

void relayOn() {
  digitalWrite(RELAY_PIN, HIGH);
  setLed(CRGB::Red);
}

void relayOff() {
  digitalWrite(RELAY_PIN, LOW);

  if (latchMode) {
    setLed(CRGB::Green);
  } else {
    setLed(CRGB::Blue);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  pinMode(RELAY_PIN, OUTPUT);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(30);

  delay(300);
  M5.update();

  if (M5.BtnA.isPressed()) {
    latchMode = true;
  } else {
    latchMode = false;
  }

  relayState = false;
  relayOff();

  if (latchMode) {
    while (M5.BtnA.isPressed()) {
      M5.update();
      delay(10);
    }
  }
}

void loop() {
  M5.update();

  bool pressed = M5.BtnA.isPressed();

  if (!latchMode) {
    if (pressed) {
      relayOn();
    } else {
      relayOff();
    }
  } else {
    if (!pressed && lastPressed) {
      relayState = !relayState;

      if (relayState) {
        relayOn();
      } else {
        relayOff();
      }
    }
  }

  lastPressed = pressed;
  delay(10);
}
