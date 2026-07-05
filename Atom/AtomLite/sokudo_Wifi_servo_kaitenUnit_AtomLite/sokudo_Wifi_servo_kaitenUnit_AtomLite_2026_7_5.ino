#include <M5Atom.h>
#include <ESP32Servo.h>

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

Servo myServo;
Preferences prefs;

// ==================================================
// Wi-Fi設定
// ==================================================
const char* password = "12345678";
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
const int BUTTON_PIN2 = 39;   // 本体ボタン GPIO39

// ==================================================
// 通信モード管理
// ==================================================
enum CommMode {
  COMM_WIFI,
  COMM_BLE
};

CommMode commMode = COMM_WIFI;

bool restartRequested = false;
unsigned long restartRequestTime = 0;

// ==================================================
// 状態管理
// ==================================================
volatile bool webPressed = false;
volatile bool blePressed = false;

bool lastPressed = false;
bool waitButtonRelease = false;
bool bootAtomButtonPressed = false;
bool bootButton1Pressed = false;
bool bootCommSwitchDone = false;
unsigned long bootPressStart = 0;
unsigned long atomHoldStart = 0;
bool atomHoldSwitchDone = false;

// GPIO39 50ms継続LOW判定用
bool g39StablePressed = false;
unsigned long g39LowStart = 0;

// ==================================================
// 通常 / 速度調整モード
// ==================================================
bool speedMode = false;
int savedAngle = 90;
int speedSetting = 200;

// 回転角ユニットとWebスライダーの共存用
int lastKnobValue = -1;
const int KNOB_CHANGE_THRESHOLD = 25;
unsigned long ignoreKnobUntil = 0;

// ==================================================
// ノンブロッキング制御
// ==================================================
bool isMoving = false;
unsigned long moveStartIdx = 0;
bool isReturning = false;

// ==================================================
// 回転角ユニット → サーボ角度
// ==================================================
int knobToAngle(int value) {
  int angle = map(value, 0, 4095, 0, 180);
  int constrainedAngle = constrain(angle, 0, 180);

  if (constrainedAngle < 5) {
    return 5;
  }

  return constrainedAngle;
}

// ==================================================
// LED
// ==================================================
void setOff() {
  M5.dis.drawpix(0, CRGB::Black);
}

void setRed() {
  M5.dis.drawpix(0, CRGB::Red);
}

void setGreen() {
  M5.dis.drawpix(0, CRGB::Green);
}

void setBlue() {
  M5.dis.drawpix(0, CRGB::Blue);
}

void setWhite() {
  M5.dis.drawpix(0, CRGB::White);
}

void setYellow() {
  M5.dis.drawpix(0, CRGB::Yellow);
}

void setWorkModeColor() {
  if (speedMode) {
    setYellow();   // 速度調整モード
  } else {
    setGreen();    // 通常モード
  }
}

void blinkWhiteTwice() {
  setWhite();
  delay(180);
  setOff();
  delay(120);
  setWhite();
  delay(180);
  setOff();
  delay(120);
}

void blinkBlueTwice() {
  setBlue();
  delay(180);
  setOff();
  delay(120);
  setBlue();
  delay(180);
  setOff();
  delay(120);
}

// ==================================================
// 通信モード保存
// ==================================================
void saveCommMode(CommMode mode) {
  prefs.begin("atomservo", false);
  prefs.putString("comm", mode == COMM_BLE ? "ble" : "wifi");
  prefs.end();
}

void saveAngle(int angle) {
  prefs.begin("atomservo", false);
  prefs.putInt("angle", angle);
  prefs.end();
}

int loadAngle() {
  prefs.begin("atomservo", true);
  int angle = prefs.getInt("angle", 90);
  prefs.end();

  return constrain(angle, 5, 180);
}

void saveSpeed(int speed) {
  prefs.begin("atomservo", false);
  prefs.putInt("speed", speed);
  prefs.end();
}

int loadSpeed() {
  prefs.begin("atomservo", true);
  int speed = prefs.getInt("speed", 200);
  prefs.end();

  return constrain(speed, 80, 400);
}

CommMode loadCommMode() {
  prefs.begin("atomservo", true);
  String mode = prefs.getString("comm", "wifi");
  prefs.end();

  if (mode == "ble") {
    return COMM_BLE;
  }

  return COMM_WIFI;
}

void requestRestart() {
  restartRequested = true;
  restartRequestTime = millis();
}

// ==================================================
// Wi-Fiチャンネル自動選択
// 1 / 6 / 11ch から混雑が少ないものを選ぶ
// ==================================================
int selectBestWiFiChannel() {
  int score1 = 0;
  int score6 = 0;
  int score11 = 0;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(100);

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

  WiFi.scanDelete();

  int bestChannel = 1;

  if (score6 < score1 && score6 <= score11) {
    bestChannel = 6;
  }

  if (score11 < score1 && score11 < score6) {
    bestChannel = 11;
  }

  return bestChannel;
}

// ==================================================
// サーボ1往復
// 動作中の追加入力は捨てる
// ==================================================
void moveServoOnce() {
  if (isMoving) return;

  setRed();

  isMoving = true;
  isReturning = false;
  moveStartIdx = millis();

  Serial.print("Start Move -> Angle: ");
  Serial.print(savedAngle);
  Serial.print(" / Speed Base: ");
  Serial.println(speedSetting);
}

// ==================================================
// Wi-Fi画面
// ==================================================
String makeRootHtml() {
  String html = "";

  html += "<!DOCTYPE html>";
  html += "<html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";

  html += "<style>";
  html += "body{margin:0;background:#050505;color:white;font-family:sans-serif;text-align:center;}";
  html += ".wrap{max-width:520px;margin:0 auto;padding:28px 18px;}";
  html += "h1{font-size:34px;margin:22px 0 24px;}";
  html += ".mainBtn{width:220px;height:220px;border-radius:50%;font-size:38px;font-weight:bold;border:8px solid #777;background:#111;color:white;box-shadow:0 0 26px #333;margin:10px auto 28px;display:block;}";
  html += ".mainBtn:active{transform:scale(0.96);}";
  html += ".row{display:flex;gap:12px;justify-content:center;flex-wrap:wrap;margin:12px 0;}";
  html += ".modeBtn{font-size:20px;padding:14px 18px;border-radius:16px;border:4px solid #555;background:#151515;color:white;min-width:150px;}";
  html += ".normalActive{border-color:#00d060;box-shadow:0 0 16px #00d060;}";
  html += ".speedActive{border-color:#ffd000;box-shadow:0 0 16px #ffd000;color:#ffe680;}";
  html += ".switchBtn{font-size:20px;padding:14px 22px;border-radius:16px;border:3px solid #4aa3ff;background:#101820;color:white;margin-top:16px;}";
  html += ".panel{background:#111;border:1px solid #333;border-radius:18px;padding:18px;margin:18px 0;text-align:left;}";
  html += ".label{display:flex;justify-content:space-between;font-size:18px;margin-bottom:8px;}";
  html += "input[type=range]{width:100%;}";
  html += ".small{font-size:14px;color:#aaa;margin-top:18px;line-height:1.5;}";
  html += "</style>";

  html += "</head><body>";
  html += "<div class='wrap'>";

  html += "<h1>Atom Servo</h1>";

  html += "<button id='goBtn' class='mainBtn' onclick='goServo()'>動かす</button>";

  html += "<div class='row'>";
  html += "<button id='normalBtn' class='modeBtn' onclick='normalMode()'>通常モード</button>";
  html += "<button id='speedBtn' class='modeBtn' onclick='speedModeBtn()'>速度調整モード</button>";
  html += "</div>";

  html += "<div class='panel'>";
  html += "<div class='label'><span>回転角度</span><span><span id='angleVal'>--</span>°</span></div>";
  html += "<input id='angleSlider' type='range' min='5' max='180' value='90' onpointerdown='sliderDragging=true' oninput='anglePreview()' onchange='setAngle()'>";
  html += "</div>";

  html += "<div class='panel'>";
  html += "<div class='label'><span>回転速度</span><span><span id='speedVal'>--</span></span></div>";
  html += "<input id='speedSlider' type='range' min='80' max='400' value='200' onpointerdown='sliderDragging=true' oninput='speedPreview()' onchange='setSpeed()'>";
  html += "<div class='small'>左ほど速く、右ほどゆっくり動きます。</div>";
  html += "</div>";

  html += "<button id='bleBtn' class='switchBtn' onclick='switchBle()'>BLEモードへ切替</button>";

  html += "<p class='small'>SSID: ";
  html += ssidName;
  html += "<br>通常モードは緑、速度調整モードは黄、動作中は赤です。</p>";

  html += "</div>";

  html += "<script>";

  html += "let currentModeSpeed=false;";
  html += "let sliderDragging=false;";

  html += "function updateModeButtons(){";
  html += "const n=document.getElementById('normalBtn');";
  html += "const s=document.getElementById('speedBtn');";
  html += "n.classList.remove('normalActive');";
  html += "s.classList.remove('speedActive');";
  html += "if(currentModeSpeed){s.classList.add('speedActive');}else{n.classList.add('normalActive');}";
  html += "}";

  html += "function applyStatus(d){";
  html += "currentModeSpeed=d.speedMode;";
  html += "document.getElementById('angleSlider').value=d.angle;";
  html += "document.getElementById('speedSlider').value=d.speed;";
  html += "document.getElementById('angleVal').textContent=d.angle;";
  html += "document.getElementById('speedVal').textContent=d.speed;";
  html += "updateModeButtons();";
  html += "}";

  html += "function loadStatus(){";
  html += "if(sliderDragging)return;";
  html += "fetch('/status').then(r=>r.json()).then(applyStatus).catch(()=>{});";
  html += "}";

  html += "function goServo(){";
  html += "const btn=document.getElementById('goBtn');";
  html += "btn.disabled=true;";
  html += "fetch('/go',{method:'POST'})";
  html += ".finally(()=>{setTimeout(()=>{btn.disabled=false;loadStatus();},300);});";
  html += "}";

  html += "function normalMode(){";
  html += "fetch('/mode_normal',{method:'POST'}).then(()=>loadStatus());";
  html += "}";

  html += "function speedModeBtn(){";
  html += "fetch('/mode_speed',{method:'POST'}).then(()=>loadStatus());";
  html += "}";

  html += "function anglePreview(){";
  html += "document.getElementById('angleVal').textContent=document.getElementById('angleSlider').value;";
  html += "}";

  html += "function speedPreview(){";
  html += "document.getElementById('speedVal').textContent=document.getElementById('speedSlider').value;";
  html += "}";

  html += "function setAngle(){";
  html += "const v=document.getElementById('angleSlider').value;";
  html += "fetch('/set_angle?value='+v,{method:'POST'})";
  html += ".then(()=>fetch('/go',{method:'POST'}))";
  html += ".finally(()=>{sliderDragging=false;setTimeout(loadStatus,300);});";
  html += "}";

  html += "function setSpeed(){";
  html += "const v=document.getElementById('speedSlider').value;";
  html += "fetch('/set_speed?value='+v,{method:'POST'})";
  html += ".then(()=>fetch('/go',{method:'POST'}))";
  html += ".finally(()=>{sliderDragging=false;setTimeout(loadStatus,300);});";
  html += "}";

  html += "function switchBle(){";
  html += "const btn=document.getElementById('bleBtn');";
  html += "btn.disabled=true;";
  html += "fetch('/switch_ble',{method:'POST'})";
  html += ".then(()=>{document.body.innerHTML='<div style=\"color:white;background:#050505;font-family:sans-serif;text-align:center;padding-top:80px;\"><h1>BLEモードへ切り替えます</h1><p>Atom Liteが自動再起動します。</p></div>';});";
  html += "}";

  html += "loadStatus();";
  html += "setInterval(loadStatus,2000);";

  html += "</script>";

  html += "</body></html>";

  return html;
}

// ==================================================
// Wi-Fi開始
// ==================================================
void startWiFiMode() {
  blinkWhiteTwice();
  setOff();

  uint64_t chipid = ESP.getEfuseMac();

  char id[7];
  sprintf(id, "%06X", (uint32_t)(chipid & 0xFFFFFF));

  ssidName = "AtomServo_" + String(id);

  int bestChannel = selectBestWiFiChannel();

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  WiFi.softAP(
    ssidName.c_str(),
    password,
    bestChannel,
    false,
    2
  );

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", makeRootHtml());
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"speedMode\":";
    json += speedMode ? "true" : "false";
    json += ",\"angle\":";
    json += String(savedAngle);
    json += ",\"speed\":";
    json += String(speedSetting);
    json += ",\"moving\":";
    json += isMoving ? "true" : "false";
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/go", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isMoving && !webPressed) {
      webPressed = true;
      request->send(200, "text/plain", "OK");
    } else {
      request->send(409, "text/plain", "BUSY");
    }
  });

  server.on("/mode_normal", HTTP_POST, [](AsyncWebServerRequest *request) {
    speedMode = false;
    setWorkModeColor();
    request->send(200, "text/plain", "NORMAL");
  });

  server.on("/mode_speed", HTTP_POST, [](AsyncWebServerRequest *request) {
    speedMode = true;
    setWorkModeColor();
    request->send(200, "text/plain", "SPEED");
  });

  server.on("/set_angle", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      int angle = request->getParam("value")->value().toInt();
      angle = constrain(angle, 5, 180);
      savedAngle = angle;
      saveAngle(savedAngle);
      lastKnobValue = analogRead(KNOB_PIN);
      ignoreKnobUntil = millis() + 3000;
      request->send(200, "text/plain", "ANGLE_OK");
    } else {
      request->send(400, "text/plain", "NO_VALUE");
    }
  });

  server.on("/set_speed", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      int speed = request->getParam("value")->value().toInt();
      speed = constrain(speed, 80, 400);

      if (speed < 90) {
        speed = 80;
      }

      speedSetting = speed;
      saveSpeed(speedSetting);
      lastKnobValue = analogRead(KNOB_PIN);
      ignoreKnobUntil = millis() + 3000;
      request->send(200, "text/plain", "SPEED_OK");
    } else {
      request->send(400, "text/plain", "NO_VALUE");
    }
  });

  server.on("/switch_ble", HTTP_POST, [](AsyncWebServerRequest *request) {
    saveCommMode(COMM_BLE);
    requestRestart();
    request->send(200, "text/plain", "SWITCHING_TO_BLE");
  });

  server.begin();

  setWorkModeColor();

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
}

// ==================================================
// BLE受信
// GO   : サーボ動作
// WIFI : Wi-Fiモードへ戻す
// ==================================================
class ServoCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();

    value.trim();
    value.toUpperCase();

    if (value == "GO") {
      if (!isMoving && !blePressed) {
        blePressed = true;
      }
    }

    if (value == "NORMAL") {
      speedMode = false;
      setWorkModeColor();
    }

   if (value == "SPEED") {
  speedMode = true;
  setWorkModeColor();
}

if (value.startsWith("ANGLE:")) {
  int angle = value.substring(6).toInt();
  angle = constrain(angle, 5, 180);

  savedAngle = angle;
  saveAngle(savedAngle);

  // BLE/HTMLで指定した角度を、直後に回転角ユニットで上書きしないため
  lastKnobValue = analogRead(KNOB_PIN);
  ignoreKnobUntil = millis() + 3000;
}

if (value.startsWith("SPEEDVALUE:")) {
  int speed = value.substring(11).toInt();
  speed = constrain(speed, 80, 400);

  if (speed < 90) {
    speed = 80;
  }

  speedSetting = speed;
saveSpeed(speedSetting);

lastKnobValue = analogRead(KNOB_PIN);
ignoreKnobUntil = millis() + 3000;
}

if (value == "WIFI") {
      saveCommMode(COMM_WIFI);
      requestRestart();
    }
  }
};

// ==================================================
// BLE開始
// ==================================================
void startBLEMode() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);

  blinkBlueTwice();
  setOff();

  BLEDevice::init(BLE_NAME);

  BLEServer *pServer = BLEDevice::createServer();

  BLEService *pService =
    pServer->createService(SERVICE_UUID);

  pCharacteristic =
    pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE
    );

  pCharacteristic->addDescriptor(new BLE2902());

  pCharacteristic->setCallbacks(new ServoCallback());

  pCharacteristic->setValue("READY");

  pService->start();

  BLEAdvertising *pAdvertising =
    BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(SERVICE_UUID);

  pAdvertising->start();

  setWorkModeColor();

  Serial.println();
  Serial.println("========== BLE MODE ==========");
  Serial.print("BLE Name : ");
  Serial.println(BLE_NAME);
  Serial.println("Send GO to move servo");
  Serial.println("Send WIFI to switch Wi-Fi mode");
}

// ==================================================
// GPIO39 50ms継続LOW判定
// ==================================================
void updateG39StableState() {
  M5.update();

  bool atomButtonPressed = M5.Btn.isPressed();

  bool rawLow =
    atomButtonPressed ||
    (digitalRead(BUTTON_PIN2) == LOW);

  unsigned long now = millis();

  if (rawLow) {
    if (g39LowStart == 0) {
      g39LowStart = now;
    }

    if (now - g39LowStart >= 50) {
      g39StablePressed = true;
    }
  } else {
    g39LowStart = 0;
    g39StablePressed = false;
  }
}

// ==================================================
// 物理入力
// 長押し判定なし
// 押した瞬間に1往復
// ==================================================
void handlePhysicalInput() {
  bool button1Pressed =
    (digitalRead(BUTTON_PIN1) == LOW);

  bool pressed =
    button1Pressed ||
    g39StablePressed;

  if (waitButtonRelease) {
    if (!pressed) {
      waitButtonRelease = false;
      lastPressed = false;
    }
    return;
  }

  if (!lastPressed && pressed) {
    moveServoOnce();
  }

  lastPressed = pressed;
}

// ==================================================
// つまみ読み取り
// Webスライダーで設定した値をすぐに上書きしないため、
// 回転角ユニットが一定以上動いたときだけ反映する
// ==================================================
void updateKnob() {
  if (millis() < ignoreKnobUntil) {
    return;
  }

  int knobValue = analogRead(KNOB_PIN);

  if (lastKnobValue < 0) {
    lastKnobValue = knobValue;
    return;
  }

  if (abs(knobValue - lastKnobValue) < KNOB_CHANGE_THRESHOLD) {
    return;
  }

  lastKnobValue = knobValue;

  if (!speedMode) {
    int newAngle = knobToAngle(knobValue);

    if (newAngle != savedAngle) {
      savedAngle = newAngle;
      saveAngle(savedAngle);
    }
  } else {
    int newSpeed = map(knobValue, 0, 4095, 80, 400);

    if (newSpeed < 90) {
      newSpeed = 80;
    }

    newSpeed = constrain(newSpeed, 80, 400);

    if (newSpeed != speedSetting) {
      speedSetting = newSpeed;
      saveSpeed(speedSetting);
    }
  }
}

// ==================================================
// サーボ更新
// ==================================================
void updateServoMove() {
  if (!isMoving) return;

  unsigned long now = millis();
  unsigned long elapsed = now - moveStartIdx;

  if (speedSetting == 80) {
    unsigned long directDriveTime =
      map(savedAngle, 0, 180, 80, 450);

    if (!isReturning) {
      myServo.write(savedAngle);

      if (elapsed >= directDriveTime) {
        isReturning = true;
        moveStartIdx = millis();
      }
    } else {
      myServo.write(0);

      if (elapsed >= directDriveTime) {
        isMoving = false;
        isReturning = false;
        setWorkModeColor();
      }
    }
  } else {
    float baseTargetTimeFor180 =
      map(speedSetting, 80, 400, 450, 3000);

    int calculatedTime =
      (int)(baseTargetTimeFor180 * ((float)savedAngle / 180.0));

    int minimumTime =
      map(savedAngle, 0, 180, 80, 450);

    int actualTime =
      max(calculatedTime, minimumTime);

    float duration = (float)actualTime;

    if (!isReturning) {
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
      if (elapsed < actualTime) {
        float progress = (float)elapsed / duration;
        int currentAngle =
          savedAngle - (int)(savedAngle * progress);

        myServo.write(currentAngle);
      } else {
        myServo.write(0);
        isMoving = false;
        isReturning = false;
        setWorkModeColor();
      }
    }
  }
}

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

  savedAngle = loadAngle();
  speedSetting = loadSpeed();

  lastKnobValue = analogRead(KNOB_PIN);

  delay(100);
  M5.update();

  bootAtomButtonPressed =
  M5.Btn.isPressed();

  bootButton1Pressed =
    (digitalRead(BUTTON_PIN1) == LOW);

  if (bootAtomButtonPressed || bootButton1Pressed) {
  speedMode = true;
  waitButtonRelease = true;

  if (bootAtomButtonPressed) {
    bootPressStart = millis();
  }

  Serial.println("Work mode: SPEED");
} else {
  speedMode = false;
  waitButtonRelease = false;
  Serial.println("Work mode: NORMAL");
}

  commMode = loadCommMode();

  if (commMode == COMM_BLE) {
    startBLEMode();
  } else {
    startWiFiMode();
  }
}

// ==================================================
// メインループ
// ==================================================
void loop() {
  updateG39StableState();

// 本体ボタンを10秒以上押し続けたら通信モード反転
bool atomButtonNow = M5.Btn.isPressed();

if (atomButtonNow) {
  if (atomHoldStart == 0) {
    atomHoldStart = millis();
  }

  if (!atomHoldSwitchDone &&
      millis() - atomHoldStart >= 10000) {

    atomHoldSwitchDone = true;

    if (commMode == COMM_WIFI) {
      saveCommMode(COMM_BLE);
      blinkBlueTwice();     // BLEへ切替合図
    } else {
      saveCommMode(COMM_WIFI);
      blinkWhiteTwice();    // Wi-Fiへ切替合図
    }

    setOff();
    requestRestart();
  }
} else {
  atomHoldStart = 0;
  atomHoldSwitchDone = false;
}

handlePhysicalInput();

  if (webPressed) {
    webPressed = false;
    moveServoOnce();
  }

  if (blePressed) {
    blePressed = false;
    moveServoOnce();
  }

  updateKnob();

  updateServoMove();

  if (restartRequested && millis() - restartRequestTime >= 800) {
    ESP.restart();
  }

  delay(1);
}
