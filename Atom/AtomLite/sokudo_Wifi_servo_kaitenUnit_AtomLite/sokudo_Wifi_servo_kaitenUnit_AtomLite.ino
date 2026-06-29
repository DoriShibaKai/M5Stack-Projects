#include <M5Atom.h>
#include <ESP32Servo.h>

#include <WiFi.h>
#include <WebServer.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

Servo myServo;

// ==================================================
// Wi-Fi設定
// ==================================================
const char* password = "12345678";
String ssidName;
WebServer server(80);

// ==================================================
// Bluetooth設定
// ==================================================
const char* BLE_NAME = "AtomServo";
#define SERVICE_UUID        "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define CHARACTERISTIC_UUID "BEB5483E-36E1-4688-B7F5-EA07361B26A8"

BLECharacteristic *pCharacteristic;
bool blePressed = false;

// ==================================================
// ピン設定
// ==================================================
const int SERVO_PIN   = 25;
const int KNOB_PIN    = 32;
const int BUTTON_PIN1 = 22;
const int BUTTON_PIN2 = 39;

// ==================================================
// 状態管理
// ==================================================
bool lastPressed = false;
bool webPressed = false;
unsigned long g39PressStart = 0;
bool g39LongPressDetected = false;

// ==================================================
// モード管理
// ==================================================
bool speedMode = false;
int savedAngle = 0;
int speedSetting = 200;

// --------------------------------------------------
// ノンブロッキング制御用の状態変数
// --------------------------------------------------
bool isMoving = false;          // 現在サーボが往復動作中か
unsigned long moveStartIdx = 0; // 動作（往路または復路）を開始した時刻(ms)
bool isReturning = false;       // 復路（帰り道）の途中か

// ==================================================
// 回転角ユニット→サーボ角度
// ==================================================
int knobToAngle(int value) {
  int angle = map(value, 0, 4095, 0, 180);
  return constrain(angle, 0, 180);
}

// ==================================================
// LED
// ==================================================
void setRed() { M5.dis.drawpix(0, CRGB::Red); }
void setGreen() { M5.dis.drawpix(0, CRGB::Green); }
void setBlue() { M5.dis.drawpix(0, CRGB::Blue); }

// ==================================================
// サーボ1往復（トリガー起動）
// ==================================================
void moveServoOnce() {
  if (isMoving) return; 

  setGreen();
  isMoving = true;
  isReturning = false;
  moveStartIdx = millis();

  Serial.print("Start Move -> Angle: ");
  Serial.print(savedAngle);
  Serial.print(" / Speed Base: ");
  Serial.println(speedSetting);
}

// ==================================================
// Wi-Fi画面 / Bluetooth受信
// ==================================================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;text-align:center;margin-top:60px;}button{font-size:32px;padding:30px 60px;border-radius:20px;}</style></head><body><h1>Atom Servo</h1><form action='/go' method='POST'><button type='submit'>動かす</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleGo() {
  webPressed = true;
  server.sendHeader("Location", "/");
  server.send(303);
}

class ServoCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();
    value.trim();
    if (value == "GO") { blePressed = true; }
  }
};

// ==================================================
// 初期設定
// ==================================================
void setup() {
  M5.begin(true, false, true);
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);
  Serial.begin(115200);

  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(0);
  setRed();

  uint64_t chipid = ESP.getEfuseMac();
  char id[7];
  sprintf(id, "%06X", (uint32_t)(chipid & 0xFFFFFF));
  ssidName = "AtomServo_" + String(id);
  WiFi.softAP(ssidName.c_str(), password);

  server.on("/", handleRoot);
  server.on("/go", HTTP_POST, handleGo);
  server.begin();

  BLEDevice::init(BLE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new ServoCallback());
  pCharacteristic->setValue("READY");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

// ==================================================
// メインループ
// ==================================================
void loop() {
  server.handleClient();

  // 物理ボタン・長押し判定
  bool pressed = (digitalRead(BUTTON_PIN1) == LOW) || (digitalRead(BUTTON_PIN2) == LOW);
  bool g39Pressed = (digitalRead(BUTTON_PIN2) == LOW);

  if (g39Pressed) {
    if (g39PressStart == 0) { g39PressStart = millis(); }
    else if (!g39LongPressDetected && millis() - g39PressStart > 800) {
      g39LongPressDetected = true;
      speedMode = !speedMode;
      if (speedMode) setBlue(); else setRed();
    }
  } else {
    g39PressStart = 0;
    g39LongPressDetected = false;
  }

  // 短押し判定
  if (!lastPressed && pressed) {
    if (!speedMode) { moveServoOnce(); }
    else if (digitalRead(BUTTON_PIN2) == LOW) { moveServoOnce(); }
  }

  if (webPressed || blePressed) {
    webPressed = false; blePressed = false;
    moveServoOnce();
  }

  // つまみの読み取り
  int knobValue = analogRead(KNOB_PIN);
  if (!speedMode) {
    savedAngle = knobToAngle(knobValue);
  } else {
    speedSetting = map(knobValue, 0, 4095, 80, 400);
    speedSetting = constrain(speedSetting, 80, 400);
  }

  // --------------------------------------------------
  // ★★★ 最速ダイレクト＆スロー補間ハイブリッド制御 ★★★
  // --------------------------------------------------
  if (isMoving) {
    unsigned long now = millis();
    unsigned long elapsed = now - moveStartIdx;

    // つまみが最速(80)にセットされている場合
    if (speedSetting == 80) {
      // サーボが物理的に180度反転するのに必要な猶予時間（450ms）
      // これを直接指令の「片道時間」として扱う（回りきらないバグを完全防止）
      unsigned long directDriveTime = map(savedAngle, 0, 180, 80, 450);

      if (!isReturning) {
        // ーー 往路：最高速度で直接目標へ ーー
        myServo.write(savedAngle);
        if (elapsed >= directDriveTime) {
          isReturning = true;
          moveStartIdx = millis(); // 復路へ切り替え
        }
      } else {
        // ーー 復路：最高速度で直接0度へ ーー
        myServo.write(0);
        if (elapsed >= directDriveTime) {
          isMoving = false;
          isReturning = false;
          if (!speedMode) setRed(); else setBlue();
        }
      }
    }
    // つまみが中速・低速（81〜400）にセットされている場合（じわーっと等速体感統一）
    else {
      // 1. つまみの設定を「180度動かすのに何msかけるか」の基準時間にする（最大3秒）
      float baseTargetTimeFor180 = map(speedSetting, 80, 400, 450, 3000);

      // 2. 角度に比例した時間を計算（体感速度の統一）
      int calculatedTime = (int)(baseTargetTimeFor180 * ((float)savedAngle / 180.0));

      // 3. 物理限界ブレーキ
      int minimumTime = map(savedAngle, 0, 180, 80, 450);
      int actualTime = max(calculatedTime, minimumTime);
      float duration = (float)actualTime;

      if (!isReturning) {
        // ーー 往路：等速補間スムーズ ーー
        if (elapsed < actualTime) {
          float progress = (float)elapsed / duration;
          int currentAngle = (int)(savedAngle * progress);
          myServo.write(currentAngle);
        } else {
          myServo.write(savedAngle);
          isReturning = true;
          moveStartIdx = millis();
        }
      } else {
        // ーー 復路：等速補間スムーズ ーー
        if (elapsed < actualTime) {
          float progress = (float)elapsed / duration;
          int currentAngle = savedAngle - (int)(savedAngle * progress);
          myServo.write(currentAngle);
        } else {
          myServo.write(0);
          isMoving = false;
          isReturning = false;
          if (!speedMode) setRed(); else setBlue();
        }
      }
    }
  }

  // 状態保存
  lastPressed = pressed;
  delay(1); 
}
