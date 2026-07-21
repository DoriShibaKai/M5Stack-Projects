#include <M5Atom.h>
#include <ESP32Servo.h>

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

Servo myServo;

// ==================================================
// 起動モード
// ==================================================

enum OperationMode {
  MODE_WIFI,
  MODE_BLE
};

OperationMode operationMode;

// 起動時に押していたボタンを離すまで物理入力を無視する
bool waitButtonRelease = false;

// ==================================================
// Wi-Fi設定
// ==================================================

const char* password = "00000000";
String ssidName;
AsyncWebServer server(80);

// ==================================================
// Bluetooth設定
// ==================================================

const char* BLE_NAME = "AtomServo";

#define SERVICE_UUID        "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define CHARACTERISTIC_UUID "BEB5483E-36E1-4688-B7F5-EA07361B26A8"

BLECharacteristic *pCharacteristic;

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

// イベントキュー
volatile bool requestQueue = false;

// 1往復終了フラグ
bool servoFinished = true;

// ==================================================
// サーボステートマシン
// ==================================================

enum ServoState {
  SERVO_IDLE,
  SERVO_MOVING_FORWARD,
  SERVO_MOVING_BACK
};

ServoState servoState = SERVO_IDLE;

unsigned long servoTimer = 0;

int targetAngle = 0;
int moveTime = 0;

// ==================================================
// 回転角ユニット → サーボ角度
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

void setBlue() {
  M5.dis.drawpix(0, CRGB::Blue);
}

// 現在のモードに応じた待機色へ戻す
void setModeColor() {

  if (operationMode == MODE_WIFI) {
    setGreen();
  } else {
    setBlue();
  }
}

// ==================================================
// イベントキューに積む
// ==================================================

void requestServo() {
  requestQueue = true;
}

// ==================================================
// サーボ動作開始（非ブロッキング）
// ==================================================

void startServoMove() {

  if (servoState != SERVO_IDLE) {
    return;
  }

  servoFinished = false;

  int value = analogRead(KNOB_PIN);

  targetAngle = knobToAngle(value);

  moveTime = servoMoveTime(targetAngle);

  Serial.printf(
    "ADC: %d  Angle: %d  Wait: %d ms\n",
    value,
    targetAngle,
    moveTime
  );

  // 入力ON・サーボ動作中は赤
  setRed();

  myServo.write(targetAngle);

  servoTimer = millis();

  servoState = SERVO_MOVING_FORWARD;
}

// ==================================================
// サーボ更新（非ブロッキング）
// ==================================================

void updateServo() {

  if (servoState == SERVO_MOVING_FORWARD) {

    if (millis() - servoTimer >= moveTime) {

      myServo.write(0);

      servoTimer = millis();

      servoState = SERVO_MOVING_BACK;
    }
  }

  else if (servoState == SERVO_MOVING_BACK) {

    if (millis() - servoTimer >= moveTime) {

      servoState = SERVO_IDLE;

      servoFinished = true;

      // 1往復終了後、現在のモード色へ戻す
      setModeColor();
    }
  }
}

// ==================================================
// Wi-Fi画面
// ==================================================

String handleRootHTML() {

  String html = "";

  html += "<!DOCTYPE html>";
  html += "<html><head>";

  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";

  html += "<style>";
  html += "body{text-align:center;margin-top:60px;font-family:sans-serif;}";
  html += "button{font-size:32px;padding:30px 60px;border-radius:20px;}";
  html += "</style>";

  html += "</head><body>";

  html += "<h1>Atom Servo</h1>";

  html += "<button id='goBtn' type='button' onclick='goServo()'>";
  html += "動かす";
  html += "</button>";

  html += "<script>";

  html += "function goServo(){";

  html += "const btn=document.getElementById('goBtn');";

  html += "btn.disabled=true;";

  html += "fetch('/go',{method:'POST'})";
  html += ".finally(()=>{btn.disabled=false;});";

  html += "}";

  html += "</script>";

  html += "</body></html>";

  return html;
}

// ==================================================
// BLE受信
// ==================================================

class ServoCallback : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {

    String value = pCharacteristic->getValue().c_str();

    value.trim();

    if (value == "GO" && servoFinished) {

      servoFinished = false;

      requestServo();
    }
  }
};

// ==================================================
// Wi-Fi開始
// ==================================================

void startWiFiMode() {

  // ------------------------------------------
  // SSID生成
  // ------------------------------------------

  uint64_t chipid = ESP.getEfuseMac();

  char id[7];

  sprintf(
    id,
    "%06X",
    (uint32_t)(chipid & 0xFFFFFF)
  );

  ssidName = "AtomServo_" + String(id);

  // ------------------------------------------
  // 周辺Wi-Fiをスキャン
  // ------------------------------------------

  WiFi.mode(WIFI_STA);

  WiFi.disconnect(true);

  delay(100);

  int score1 = 0;
  int score6 = 0;
  int score11 = 0;

  int n = WiFi.scanNetworks();

  for (int i = 0; i < n; i++) {

    int ch = WiFi.channel(i);

    int rssi = WiFi.RSSI(i);

    int power = 100 + rssi;

    if (ch >= 1 && ch <= 3) {
      score1 += power;
    }

    if (ch >= 4 && ch <= 8) {
      score6 += power;
    }

    if (ch >= 9 && ch <= 13) {
      score11 += power;
    }
  }

  // ------------------------------------------
  // 1 / 6 / 11 から選択
  // ------------------------------------------

  int bestChannel = 1;

  if (score6 < score1 && score6 <= score11) {
    bestChannel = 6;
  }

  if (score11 < score1 && score11 < score6) {
    bestChannel = 11;
  }

  // ------------------------------------------
  // AP開始
  // ------------------------------------------

  WiFi.mode(WIFI_AP);

  WiFi.setSleep(false);

  WiFi.softAP(
    ssidName.c_str(),
    password,
    bestChannel,
    false,
    2
  );

  Serial.println();
  Serial.println("========== Wi-Fi MODE ==========");

  Serial.print("SSID : ");
  Serial.println(ssidName);

  Serial.print("PASS : ");
  Serial.println(password);

  Serial.print("Channel : ");
  Serial.println(bestChannel);

  Serial.print("Open : http://");
  Serial.println(WiFi.softAPIP());

  // ------------------------------------------
  // Webサーバー
  // ------------------------------------------

  server.on(
    "/",
    HTTP_GET,
    [](AsyncWebServerRequest *request) {

      request->send(
        200,
        "text/html",
        handleRootHTML()
      );
    }
  );

  server.on(
    "/go",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {

      if (servoFinished) {

        servoFinished = false;

        requestServo();

        request->send(
          200,
          "text/plain",
          "OK"
        );

      } else {

        request->send(
          409,
          "text/plain",
          "BUSY"
        );
      }
    }
  );

  server.begin();

  // Wi-Fiモード待機色
  setGreen();
}

// ==================================================
// BLE開始
// ==================================================

void startBLEMode() {

  BLEDevice::init(BLE_NAME);

  BLEServer *pServer =
    BLEDevice::createServer();

  BLEService *pService =
    pServer->createService(SERVICE_UUID);

  pCharacteristic =
    pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE
    );

  pCharacteristic->addDescriptor(
    new BLE2902()
  );

  pCharacteristic->setCallbacks(
    new ServoCallback()
  );

  pCharacteristic->setValue("READY");

  pService->start();

  BLEAdvertising *pAdvertising =
    BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(
    SERVICE_UUID
  );

  pAdvertising->start();

  Serial.println();
  Serial.println("========== BLE MODE ==========");

  Serial.print("BLE Name : ");
  Serial.println(BLE_NAME);

  Serial.println("Waiting BLE connection...");

  // BLEモード待機色
  setBlue();
}

// ==================================================
// 初期設定
// ==================================================

void setup() {

  // Atom Lite初期化
  M5.begin(true, false, true);

  // 物理入力
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);

  // シリアルモニタ
  Serial.begin(115200);

  // サーボ初期設定
  myServo.setPeriodHertz(50);

  myServo.attach(
    SERVO_PIN,
    500,
    2400
  );

  myServo.write(0);

  // ------------------------------------------
  // 起動モード判定
  // ------------------------------------------

  delay(100);

  M5.update();

  bool atomButtonPressed =
    M5.Btn.isPressed();

  bool gpio22Pressed =
    (digitalRead(BUTTON_PIN1) == LOW);

  // 本体ボタンまたはGPIO22を押しながら起動
  // → BLEモード
  if (atomButtonPressed || gpio22Pressed) {

    operationMode = MODE_BLE;

    // 起動時に押していた入力を
    // サーボ操作として誤認しない
    waitButtonRelease = true;

    Serial.println("Boot mode: BLE");
  }

  // 通常起動
  // → Wi-Fiモード
  else {

    operationMode = MODE_WIFI;

    waitButtonRelease = false;

    Serial.println("Boot mode: Wi-Fi");
  }

  // ------------------------------------------
  // 選択された通信方式だけ起動
  // ------------------------------------------

  if (operationMode == MODE_WIFI) {

    startWiFiMode();

  } else {

    startBLEMode();
  }
}

// ==================================================
// メインループ
// ==================================================

void loop() {

  M5.update();

  // ------------------------------------------
  // 物理ボタン
  // ------------------------------------------

  static unsigned long pin39LowStart = 0;

  bool atomButtonPressed =
    M5.Btn.isPressed();

  bool pin22Pressed =
    (digitalRead(BUTTON_PIN1) == LOW);

  bool pin39RawLow =
    (digitalRead(BUTTON_PIN2) == LOW);

  bool pin39Pressed = false;

  // ------------------------------------------
  // GPIO39
  // 50ms継続LOWで押下判定
  // ------------------------------------------

  if (pin39RawLow) {

    if (pin39LowStart == 0) {

      pin39LowStart = millis();
    }

    if (millis() - pin39LowStart >= 50) {

      pin39Pressed = true;
    }

  } else {

    pin39LowStart = 0;
  }

  // 3種類の物理入力
  bool pressed =
    atomButtonPressed ||
    pin22Pressed ||
    pin39Pressed;

  // ------------------------------------------
  // BLEモード選択時
  // 起動時に押していたボタンを
  // 一度離すまで入力無視
  // ------------------------------------------

  if (waitButtonRelease) {

    if (!atomButtonPressed && !pin22Pressed) {

      waitButtonRelease = false;

      lastPressed = false;
    }

    updateServo();

    delay(2);

    return;
  }

  // ------------------------------------------
  // 押した瞬間だけ受付
  // ------------------------------------------

  if (
    !lastPressed &&
    pressed &&
    servoFinished
  ) {

    servoFinished = false;

    requestServo();
  }

  // 状態保存
  lastPressed = pressed;

  // ------------------------------------------
  // キュー処理
  // ------------------------------------------

  if (
    servoState == SERVO_IDLE &&
    requestQueue
  ) {

    requestQueue = false;

    startServoMove();
  }

  // ------------------------------------------
  // サーボ更新
  // ------------------------------------------

  updateServo();

  delay(2);
}
