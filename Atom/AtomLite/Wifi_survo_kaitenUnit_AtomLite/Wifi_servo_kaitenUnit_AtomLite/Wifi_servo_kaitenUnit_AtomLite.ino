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

// パスワード（8文字以上）変えてください。
const char* password = "00000000";

// SSID（Wi-Fi名）はMACアドレスから自動生成
String ssidName;

// Webサーバー
WebServer server(80);

// ==================================================
// Bluetooth設定
// ==================================================

// Bluetooth名
const char* BLE_NAME = "AtomServo";

// BLE UUID
#define SERVICE_UUID        "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define CHARACTERISTIC_UUID "BEB5483E-36E1-4688-B7F5-EA07361B26A8"

BLECharacteristic *pCharacteristic;

// BLEから動作要求が来たか
bool blePressed = false;

// ==================================================
// ピン設定
// ==================================================

const int SERVO_PIN   = 25;
const int KNOB_PIN    = 32;
const int BUTTON_PIN1 = 33;
const int BUTTON_PIN2 = 39;

// ==================================================
// 状態管理
// ==================================================

bool lastPressed = false;

// Wi-Fi画面から押された
bool webPressed = false;

// ==================================================
// 回転角ユニット→サーボ角度
// ==================================================

int knobToAngle(int value) {

  int angle = map(value, 0, 4095, 0, 180);

  return constrain(angle, 0, 180);
}

// ==================================================
// サーボ移動時間
// ==================================================

int servoMoveTime(int angle) {

  int t = map(angle, 0, 180, 80, 400);

  return constrain(t, 80, 400);
}

// ==================================================
// LED
// ==================================================

void setRed() {
  M5.dis.drawpix(0, CRGB::Red);
}

void setGreen() {
  M5.dis.drawpix(0, CRGB::Green);
}

// ==================================================
// サーボ1往復
//
// どこから呼んでも同じ動作
// ==================================================

void moveServoOnce() {

  setGreen();

  int value = analogRead(KNOB_PIN);

  int targetAngle = knobToAngle(value);

  int moveTime = servoMoveTime(targetAngle);

  Serial.print("ADC: ");
  Serial.print(value);

  Serial.print("  Angle: ");
  Serial.print(targetAngle);

  Serial.print("  Wait: ");
  Serial.print(moveTime);

  Serial.println(" ms");

  myServo.write(targetAngle);

  delay(moveTime);

  myServo.write(0);

  delay(moveTime);

  setRed();
}

// ==================================================
// Wi-Fi画面
// ==================================================

void handleRoot() {

  String html = "";

  html += "<!DOCTYPE html>";
  html += "<html><head>";

  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";

  html += "<style>";
  html += "body{font-family:sans-serif;text-align:center;margin-top:60px;}";
  html += "button{font-size:32px;padding:30px 60px;border-radius:20px;}";
  html += "</style>";

  html += "</head><body>";

  html += "<h1>Atom Servo</h1>";

  html += "<form action='/go' method='POST'>";
  html += "<button type='submit'>動かす</button>";
  html += "</form>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleGo() {

  webPressed = true;

  server.sendHeader("Location", "/");

  server.send(303);
}

// ==================================================
// Bluetooth受信
//
// iPhone/iPad/PCから文字列
// "GO"
// を受信するとサーボを動かす
// ==================================================

class ServoCallback : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {

    String value = pCharacteristic->getValue().c_str();

    value.trim();

    if (value == "GO") {

      blePressed = true;

    }

  }

};

// ==================================================
// 初期設定
// ==================================================

void setup() {

  // Atom Lite初期化
  M5.begin(true, false, true);

  // ボタン設定
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);

  // シリアルモニタ
  Serial.begin(115200);

  // サーボ初期設定
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(0);

  setRed();

  // ------------------------------------------
  // Wi-Fi名(SSID)をMACアドレスから自動生成
  // ------------------------------------------

  uint64_t chipid = ESP.getEfuseMac();

  char id[7];

  sprintf(id, "%06X", (uint32_t)(chipid & 0xFFFFFF));

  ssidName = "AtomServo_" + String(id);

  // Atom Lite自身がWi-Fiアクセスポイントになる
  WiFi.softAP(ssidName.c_str(), password);

  Serial.println();
  Serial.println("========== Wi-Fi ==========");

  Serial.print("SSID : ");
  Serial.println(ssidName);

  Serial.print("PASS : ");
  Serial.println(password);

  Serial.print("Open : http://");
  Serial.println(WiFi.softAPIP());

  // Webサーバー開始
  server.on("/", handleRoot);
  server.on("/go", HTTP_POST, handleGo);
  server.begin();

  // ------------------------------------------
  // Bluetooth(BLE)開始
  // ------------------------------------------

  BLEDevice::init(BLE_NAME);

  BLEServer *pServer = BLEDevice::createServer();

  BLEService *pService =
      pServer->createService(SERVICE_UUID);

  pCharacteristic =
      pService->createCharacteristic(
          CHARACTERISTIC_UUID,
          BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE);

  pCharacteristic->addDescriptor(new BLE2902());

  pCharacteristic->setCallbacks(new ServoCallback());

  pCharacteristic->setValue("READY");

  pService->start();

  BLEAdvertising *pAdvertising =
      BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(SERVICE_UUID);

  pAdvertising->start();

  Serial.println();
  Serial.println("======= Bluetooth =======");

  Serial.print("BLE Name : ");
  Serial.println(BLE_NAME);

  Serial.println("Waiting BLE connection...");
}

// ==================================================
// メインループ
// ==================================================

void loop() {

  // Wi-Fiアクセス受付
  server.handleClient();

  // -------------------------
  // 物理ボタン
  // -------------------------

  bool pressed =
      (digitalRead(BUTTON_PIN1) == LOW) ||
      (digitalRead(BUTTON_PIN2) == LOW);

  // 押した瞬間だけ動作
  if (!lastPressed && pressed) {

    moveServoOnce();

  }

  // -------------------------
  // Wi-Fiボタン
  // -------------------------

  if (webPressed) {

    webPressed = false;

    moveServoOnce();

  }

  // -------------------------
  // Bluetooth命令
  // -------------------------

  if (blePressed) {

    blePressed = false;

    moveServoOnce();

  }

  // 状態保存
  lastPressed = pressed;

  delay(5);
}
