#include <M5Unified.h>
#include <FastLED.h>

const int RELAY_PIN = 2;

// ATOM S3 Lite 内蔵RGB LED
const int LED_PIN = 35;
const int NUM_LEDS = 4;
CRGB leds[NUM_LEDS];

bool latchMode = false;   // false=ダイレクト, true=ラッチ
bool relayState = false;
bool lastPressed = false;

void setLed(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
  FastLED.show();
}

void relayOn() {
  digitalWrite(RELAY_PIN, HIGH);
  setLed(CRGB::Red);      // ON中は赤
}

void relayOff() {
  digitalWrite(RELAY_PIN, LOW);

  if (latchMode) {
    setLed(CRGB::Green);  // ラッチOFFは緑
  } else {
    setLed(CRGB::Blue);   // ダイレクトOFFは青
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

  // 電源ONまたはリセット後、本体ボタンを押していたらラッチ
  if (M5.BtnA.isPressed()) {
    latchMode = true;
  } else {
    latchMode = false;
  }

  relayState = false;
  relayOff();

  // 起動時に押していた場合、離すまで待つ
  while (M5.BtnA.isPressed()) {
    M5.update();
    delay(10);
  }
}

void loop() {
  M5.update();

  bool pressed = M5.BtnA.isPressed();

  if (!latchMode) {
    // ダイレクト：押している間だけON
    if (pressed) {
      relayOn();
    } else {
      relayOff();
    }
  } else {
    // ラッチ：押して離したらON/OFF切替
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
