#include <M5Unified.h>
#include <Unit_Encoder.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==================================================
// モード切替ボタン
// ==================================================
const int MODE_BUTTON_PIN = 7;  // Port BのG7

// ==================================================
// リレー
// ==================================================
const int INPUT_BUTTON_PIN = 8; // 外部入力スイッチ
const int RELAY_PIN = 2;


// ==================================================
// ATOMS3R 画面表示（スプライトの宣言）
// ==================================================
uint32_t normalBackgroundColor = TFT_PURPLE;
LGFX_Sprite spr(&M5.Display); // チラつき防止用の裏画面

// ==================================================
// タイマー設定
// ==================================================
unsigned long timerDuration = 2000;

// タイマー設定保存用
bool timerSavePending = false;
unsigned long timerLastChangedTime = 0;
const unsigned long TIMER_SAVE_DELAY_MS = 1000;

// ==================================================
// I2Cエンコーダ
// ==================================================
Unit_Encoder encoder;
int16_t lastEncoderValue = 0;
Preferences preferences; // タイマー時間・動作モード用

// ==================================================
// Wi-Fi設定
// ==================================================
const char* password = "12345678";
String ssidName;

AsyncWebServer server(80);

// ==================================================
// BLE設定
// ==================================================
const char* BLE_NAME = "AtomRelay";

#define SERVICE_UUID "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define CHARACTERISTIC_UUID "BEB5483E-36E1-4688-B7F5-EA07361B26A8"

BLECharacteristic *pCharacteristic = nullptr;

// 通信相手の接続状態
bool wifiClientConnected = false;

bool bleClientConnected = false;

unsigned long wifiLastAccessTime = 0;

// ==================================================
// Preferences
// ==================================================
Preferences prefs; // Wi-Fi／BLE用

// ==================================================
// 通信モード
// ==================================================
enum CommMode {
  COMM_WIFI,
  COMM_BLE
};

CommMode commMode = COMM_WIFI;

bool restartRequested = false;
unsigned long restartRequestTime = 0;

// ==================================================
// 動作モード
// ==================================================
enum Mode {
  DIRECT_MODE,
  LATCH_MODE,
  TIMER_MODE
};

Mode currentMode = DIRECT_MODE;

// ==================================================
// モード操作元
// ==================================================
enum ModeSource {
  SOURCE_PHYSICAL,
  SOURCE_BROWSER
};

ModeSource modeSource = SOURCE_PHYSICAL;

// ブラウザ/BLEから最後に指定されたモード
Mode browserMode = DIRECT_MODE;

// ==================================================
// 状態管理
// ==================================================
bool relayState = false;
bool lastPhysicalPressed = false;

// モード切替ボタンの状態
bool lastModeButtonPressed = false;
unsigned long modeButtonLastChangeTime = 0;
const unsigned long MODE_BUTTON_DEBOUNCE_MS = 50;

bool timerRunning = false;
unsigned long timerStartTime = 0;
int lastDisplayedSeconds = -1;

// 本体ボタン10秒長押しで Wi-Fi / BLE 切替
unsigned long atomHoldStart = 0;
bool atomHoldSwitchDone = false;

// Wi-Fi操作要求
volatile bool webHolding = false;
volatile bool webToggleRequest = false;
volatile bool webTimerRequest = false;

// BLE操作要求
volatile bool bleHolding = false;
volatile bool bleToggleRequest = false;
volatile bool bleTimerRequest = false;

// 通信からのモード変更要求
volatile bool modeChangeRequested = false;
volatile Mode requestedMode = DIRECT_MODE;

// プロトタイプ宣言（関数の順序エラー防止）
void saveOperationMode(Mode mode);
Mode loadOperationMode();
void drawScreen();
void relayOff();
void relayOn();
void sendBleStatus();

// ==================================================
// 画面表示関連の関数
// ==================================================
String timerText() {
  unsigned long totalSeconds = timerDuration / 1000;

  if (totalSeconds < 60) {
    return String(totalSeconds) + "秒";
  }

  unsigned long hours = totalSeconds / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  if (hours == 0) {
    if (seconds == 0) {
      return String(minutes) + "分";
    }
    return String(minutes) + "分" + String(seconds) + "秒";
  }

  if (minutes == 0) {
    return String(hours) + "時間";
  }

  return String(hours) + "時間" + String(minutes) + "分";
}

void updateTimerCountdown() {
  if (!timerRunning || currentMode != TIMER_MODE) {
    return;
  }

  unsigned long elapsed = millis() - timerStartTime;
  long remainingMs = (long)timerDuration - (long)elapsed;

  int remainingSeconds;
  if (remainingMs <= 0) {
    remainingSeconds = 0;
  }
  else {
    remainingSeconds = (remainingMs + 999) / 1000;
  }

  if (remainingSeconds == lastDisplayedSeconds) {
    return;
  }

  lastDisplayedSeconds = remainingSeconds;

  // カウントダウン表示位置を設定時間表示と完全に一致させる
  int barHeight = 24;
  int base_y = barHeight + (M5.Display.height() - barHeight) / 2 - 15;
  int targetY = base_y + 35;

  uint16_t redBackground = M5.Display.color565(230, 40, 40);
  uint16_t whiteText = TFT_WHITE;

  // 文字のはみ出しを防ぐため、1行分を綺麗に塗りつぶす
  int h = 26;
  M5.Display.fillRect(0, targetY - (h / 2), M5.Display.width(), h, redBackground);

  M5.Display.setFont(&fonts::lgfxJapanGothic_20);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(whiteText);

  M5.Display.drawString(
    String(remainingSeconds) + "秒",
    M5.Display.width() / 2,
    targetY
  );
}

void drawScreen() {
  uint16_t backgroundColor;
  uint16_t textColor;
  uint16_t statusBarBg = spr.color565(30, 30, 30);
  String modeText;

  if (relayState) {
    backgroundColor = spr.color565(230, 40, 40);
    textColor = TFT_WHITE;
  }
  else if (currentMode == DIRECT_MODE) {
    backgroundColor = spr.color565(20, 100, 240);
    textColor = TFT_WHITE;
  }
  else if (currentMode == LATCH_MODE) {
    backgroundColor = spr.color565(10, 150, 70);
    textColor = TFT_WHITE;
  }
  else {
    backgroundColor = spr.color565(245, 190, 0);
    textColor = TFT_BLACK;
  }

  if (currentMode == DIRECT_MODE) modeText = "ダイレクト";
  else if (currentMode == LATCH_MODE) modeText = "ラッチ";
  else modeText = "タイマー";

  // スプライト（裏画面）のクリア
  spr.fillScreen(backgroundColor);

  // 上部ステータスバー
  int barHeight = 24;
  spr.fillRect(0, 0, spr.width(), barHeight, statusBarBg);
  spr.drawFastHLine(0, barHeight - 1, spr.width(), spr.color565(60, 60, 60));

 
// 通信モードタグ（カプセル表現）
String commText =
  (commMode == COMM_WIFI) ? "Wi-Fi" : "BLE";

bool clientConnected =
  (commMode == COMM_WIFI)
    ? wifiClientConnected
    : bleClientConnected;

uint16_t tagBg =
  (commMode == COMM_WIFI)
    ? spr.color565(0, 120, 200)
    : spr.color565(100, 100, 100);

uint16_t connectionColor =
  clientConnected
    ? spr.color565(40, 220, 80)
    : spr.color565(150, 150, 150);

int tagWidth = 52;
int tagHeight = 16;
int tagX = spr.width() - tagWidth - 6;
int tagY = (barHeight - tagHeight) / 2;

spr.fillRoundRect(
  tagX,
  tagY,
  tagWidth,
  tagHeight,
  4,
  tagBg
);

// 左側に接続状態の丸を描く
spr.fillCircle(
  tagX + 8,
  tagY + tagHeight / 2,
  3,
  connectionColor
);

// 丸の右側に Wi-Fi／BLE を描く
spr.setFont(&fonts::Font2);
spr.setTextSize(1);
spr.setTextDatum(middle_left);
spr.setTextColor(TFT_WHITE);

spr.drawString(
  commText,
  tagX + 15,
  tagY + tagHeight / 2 + 1
);

  spr.setTextDatum(middle_left);
  spr.setTextColor(spr.color565(180, 180, 180));
  spr.drawString("ATOM", 8, barHeight / 2 + 1);

  // 中央の動作モード表示
  spr.setFont(&fonts::lgfxJapanGothic_24);
  spr.setTextSize(1);
  spr.setTextDatum(middle_center);
  spr.setTextColor(textColor);

  int x = spr.width() / 2;
  int y = barHeight + (spr.height() - barHeight) / 2 - 15;
  spr.drawString(modeText, x, y);
  spr.drawString(modeText, x + 1, y);

  // タイマー設定時間の表示
  if (currentMode == TIMER_MODE) {
    spr.setTextDatum(middle_center);
    spr.drawString(
      timerText(),
      spr.width() / 2,
      y + 35
    );
  }

  // 完成した画面を一瞬で本番液晶に転送（チラつき防止）
  spr.pushSprite(0, 0);
}

void showModeLed() {
  drawScreen();
}

void blinkWhiteTwice() {
  drawScreen();
}

void blinkBlueTwice() {
  drawScreen();
}

// ==================================================
// リレー制御
// ==================================================
void relayOn() {
  if (relayState) return;

  relayState = true;
  digitalWrite(RELAY_PIN, HIGH);
  drawScreen();
}

void relayOff() {
  if (!relayState) return;

  relayState = false;
  digitalWrite(RELAY_PIN, LOW);
  drawScreen();
}

String modeToString(Mode mode) {
  if (mode == DIRECT_MODE) return "DIRECT";
  if (mode == LATCH_MODE) return "LATCH";
  return "TIMER";
}

void sendBleStatus() {
  if (commMode != COMM_BLE) return;
  if (pCharacteristic == nullptr) return;

  String status = "STATUS:";

  status += "MODE:";
  status += modeToString(currentMode);

  status += ",RELAY:";
  status += relayState ? "ON" : "OFF";

  status += ",TIME:";
  status += String(timerDuration / 1000UL);

  pCharacteristic->setValue(status.c_str());
  pCharacteristic->notify();
}

void applyModeChange(Mode newMode) {
  if (currentMode == newMode) return;

  currentMode = newMode;
  saveOperationMode(currentMode);

  // タイマーモードへ入った瞬間のエンコーダ位置を基準にする
if (currentMode == TIMER_MODE) {
  lastEncoderValue = encoder.getEncoderValue();
}

  timerRunning = false;

  webHolding = false;
  webToggleRequest = false;
  webTimerRequest = false;

  bleHolding = false;
  bleToggleRequest = false;
  bleTimerRequest = false;

  digitalWrite(RELAY_PIN, LOW);
  relayState = false;

  showModeLed();
  sendBleStatus();
}

void updateModeButton() {
  bool pressed = digitalRead(MODE_BUTTON_PIN) == LOW;

  if (pressed != lastModeButtonPressed &&
      millis() - modeButtonLastChangeTime >= MODE_BUTTON_DEBOUNCE_MS) {

    modeButtonLastChangeTime = millis();
    lastModeButtonPressed = pressed;

    if (pressed) {
      modeSource = SOURCE_PHYSICAL;

      if (currentMode == DIRECT_MODE) {
        applyModeChange(LATCH_MODE);
      }
      else if (currentMode == LATCH_MODE) {
        applyModeChange(TIMER_MODE);
      }
      else {
        applyModeChange(DIRECT_MODE);
      }
    }
  }
}

// ==================================================
// Preferences 関連
// ==================================================
void saveCommMode(CommMode mode) {
  prefs.begin("atomrelay", false);
  prefs.putString("comm", mode == COMM_BLE ? "ble" : "wifi");
  prefs.end();
}

CommMode loadCommMode() {
  prefs.begin("atomrelay", true);
  String mode = prefs.getString("comm", "wifi");
  prefs.end();

  if (mode == "ble") return COMM_BLE;
  return COMM_WIFI;
}

void saveOperationMode(Mode mode) {
  preferences.putInt("opmode", (int)mode);
}

Mode loadOperationMode() {
  int savedMode = preferences.getInt("opmode", (int)DIRECT_MODE);

  if (savedMode < (int)DIRECT_MODE || savedMode > (int)TIMER_MODE) {
    return DIRECT_MODE;
  }
  return (Mode)savedMode;
}

void requestRestart() {
  restartRequested = true;
  restartRequestTime = millis();
}

// ==================================================
// Wi-Fi Webサーバー関連
// ==================================================
String makeRootHtml() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">

<head>
  <meta charset="UTF-8">
  <title>Atom Relay Wi-Fi</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <style>
    body {
      margin: 0;
      background: #050505;
      color: white;
      font-family: sans-serif;
      text-align: center;
    }

    .wrap {
      max-width: 520px;
      margin: 0 auto;
      padding: 32px 18px;
    }

    h1 {
      font-size: 28px;
      font-weight: 600;
      letter-spacing: 1px;
      margin: 0 0 24px;
      color: #fff;
    }

    /* 上部ボタン */
    .topRow {
      display: flex;
      gap: 12px;
      justify-content: center;
      margin-bottom: 32px;
    }

    .topBtn {
      font-size: 14px;
      font-weight: bold;
      padding: 10px 20px;
      border-radius: 20px;
      border: 2px solid #2d4f7c;
      background: #0a1118;
      color: #4aa3ff;
      cursor: pointer;
      transition: all 0.2s;
    }

    .topBtn:active {
      background: #102236;
      transform: scale(0.95);
    }

    /* メインONボタン */
    .mainBtn {
      width: 220px;
      height: 220px;
      border-radius: 50%;
      font-size: 46px;
      font-weight: bold;
      border: 6px solid #333;
      background: radial-gradient(circle, #1a1a1a 0%, #111 100%);
      color: #666;
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
      margin: 0 auto 36px;
      display: block;
      touch-action: none;
      user-select: none;
      -webkit-user-select: none;
      transition: all 0.15s;
    }

    .mainBtn.active {
      color: #fff;
      border-color: #ff3b30;
      box-shadow:
        0 0 16px rgba(255, 59, 48, 0.8),
        0 0 35px rgba(255, 59, 48, 0.4);
    }

    /* モードボタン */
    .modeRow {
      display: flex;
      gap: 10px;
      justify-content: center;
      margin-bottom: 24px;
    }

    .modeBtn {
      font-size: 15px;
      font-weight: 500;
      padding: 12px 20px;
      border-radius: 16px;
      border: 2px solid #222;
      background: #111;
      color: #888;
      cursor: pointer;
      transition: all 0.25s;
    }

    .directActive {
      border-color: #007aff;
      color: #fff;
      box-shadow: 0 0 15px rgba(0, 122, 255, 0.4);
      background: #001a33;
    }

    .latchActive {
      border-color: #34c759;
      color: #fff;
      box-shadow: 0 0 15px rgba(52, 199, 89, 0.4);
      background: #052e14;
    }

    .timerActive {
      border-color: #ff9500;
      color: #fff;
      box-shadow: 0 0 15px rgba(255, 149, 0, 0.4);
      background: #2e1a00;
    }

    /* タイマー設定 */
    .timerConfig {
      display: none;
      margin: 24px auto 12px;
      animation: fadeIn 0.3s ease-out;
    }

    @keyframes fadeIn {
      from {
        opacity: 0;
        transform: translateY(-10px);
      }

      to {
        opacity: 1;
        transform: translateY(0);
      }
    }

    .timerInputWrapper {
      display: inline-flex;
      align-items: center;
      background: #16161a;
      border: 1px solid #2c2c35;
      padding: 6px 14px;
      border-radius: 24px;
      margin-right: 12px;
      vertical-align: middle;
    }

    .timerInputWrapper input {
      font-size: 20px;
      font-weight: bold;
      padding: 4px;
      width: 80px;
      text-align: center;
      border: none;
      background: transparent;
      color: #ff9500;
      outline: none;
    }

    .timerInputWrapper input::-webkit-outer-spin-button,
    .timerInputWrapper input::-webkit-inner-spin-button {
      -webkit-appearance: none;
      margin: 0;
    }

    .timerInputWrapper .unit {
      color: #666;
      font-size: 14px;
      margin-left: 4px;
      padding-right: 4px;
    }

    .timerConfig button {
      font-size: 15px;
      font-weight: bold;
      padding: 12px 24px;
      border-radius: 24px;
      border: none;
      background: #ff9500;
      color: #000;
      cursor: pointer;
      vertical-align: middle;
      transition: all 0.2s;
      box-shadow: 0 4px 12px rgba(255, 149, 0, 0.2);
    }

    .timerConfig button:active {
      transform: scale(0.96);
      opacity: 0.9;
    }

    #status {
      font-size: 13px;
      color: #666;
      margin-top: 32px;
      font-family: monospace;
      letter-spacing: 0.5px;
    }
  </style>
</head>

<body>

  <div class="wrap">

    <h1>Atom Relay Wi-Fi</h1>

    <div class="topRow">
      <button class="topBtn" onclick="switchBle()">
        BLEモードへ切替
      </button>
    </div>

    <button id="relayBtn" class="mainBtn">ON</button>

    <div class="modeRow">
      <button
        id="directBtn"
        class="modeBtn"
        onclick="setMode('DIRECT')">
        ダイレクト
      </button>

      <button
        id="latchBtn"
        class="modeBtn"
        onclick="setMode('LATCH')">
        ラッチ
      </button>

      <button
        id="timerBtn"
        class="modeBtn"
        onclick="setMode('TIMER')">
        タイマー
      </button>
    </div>

    <div id="timerPanel" class="timerConfig">

      <div class="timerInputWrapper">
        <input
          type="number"
          id="timerInput"
          min="1"
          value="2">

        <span class="unit">秒</span>
      </div>

      <button onclick="sendTimerTime()">
        設定する
      </button>

    </div>

    <p id="status">STATUS: Connected</p>

  </div>

<script>

const relayBtn = document.getElementById("relayBtn");
const timerInput =
  document.getElementById("timerInput");

  timerInput.addEventListener("focus", () => {
  timerEditing = true;
});

timerInput.addEventListener("blur", () => {
  timerEditing = false;
});

let currentMode = "DIRECT";
let latchRelayOn = false;
let timerActiveTimeout = null;
let timerEditing = false;

// ==================================================
// モード表示
// ==================================================

function updateModeDisplay(mode) {

  currentMode = mode;

  const directBtn =
    document.getElementById("directBtn");

  const latchBtn =
    document.getElementById("latchBtn");

  const timerBtn =
    document.getElementById("timerBtn");

  const timerPanel =
    document.getElementById("timerPanel");

  directBtn.classList.remove("directActive");
  latchBtn.classList.remove("latchActive");
  timerBtn.classList.remove("timerActive");

  if (mode === "DIRECT") {

    directBtn.classList.add("directActive");
    timerPanel.style.display = "none";
  }

  else if (mode === "LATCH") {

    latchBtn.classList.add("latchActive");
    timerPanel.style.display = "none";
  }

  else if (mode === "TIMER") {

    timerBtn.classList.add("timerActive");
    timerPanel.style.display = "block";
  }
}


// ==================================================
// リレー操作
// ==================================================

function pressRelay(e) {

  e.preventDefault();

  if (currentMode === "DIRECT") {

    relayBtn.classList.add("active");
  }

  else if (currentMode === "LATCH") {

    latchRelayOn = !latchRelayOn;

    relayBtn.classList.toggle(
      "active",
      latchRelayOn
    );
  }

  else if (currentMode === "TIMER") {

    relayBtn.classList.add("active");

    if (timerActiveTimeout !== null) {
      clearTimeout(timerActiveTimeout);
    }

    const seconds =
      Number(
        document.getElementById("timerInput").value
      );

    timerActiveTimeout = setTimeout(() => {

      relayBtn.classList.remove("active");
      timerActiveTimeout = null;

    }, seconds * 1000);
  }

  fetch(
    "/press",
    {
      method: "POST",
      cache: "no-store"
    }
  );
}


function releaseRelay(e) {

  e.preventDefault();

  if (currentMode === "DIRECT") {

    relayBtn.classList.remove("active");
  }

  fetch(
    "/release",
    {
      method: "POST",
      cache: "no-store"
    }
  );
}


relayBtn.addEventListener(
  "pointerdown",
  pressRelay
);

relayBtn.addEventListener(
  "pointerup",
  releaseRelay
);

relayBtn.addEventListener(
  "pointercancel",
  releaseRelay
);

relayBtn.addEventListener(
  "pointerleave",
  releaseRelay
);


// ==================================================
// モード変更
// ==================================================

function setMode(mode) {

  fetch(
    "/set_mode?value=" + mode,
    {
      method: "POST",
      cache: "no-store"
    }
  )
  .then(() => {

    updateModeDisplay(mode);

    relayBtn.classList.remove("active");
    latchRelayOn = false;

    if (timerActiveTimeout !== null) {

      clearTimeout(timerActiveTimeout);
      timerActiveTimeout = null;
    }

    document.getElementById("status").textContent =
      "STATUS: Mode " + mode;
  });
}


// ==================================================
// タイマー時間設定
// ==================================================

function sendTimerTime() {

  const seconds =
    Number(
      document.getElementById("timerInput").value
    );

  if (!seconds || seconds < 1) {

    alert("1秒以上の値を入力してください");
    return;
  }

  fetch(
    "/set_timer?seconds=" + seconds,
    {
      method: "POST",
      cache: "no-store"
    }
  )
  .then(() => {

     timerEditing = false;

    document.getElementById("status").textContent =
      "STATUS: Timer " + seconds + " sec";
  });
}

// ==================================================
// 本体のタイマー時間をブラウザへ反映
// ==================================================

function loadStatus() {

  fetch(
    "/status",
    {
      method: "GET",
      cache: "no-store"
    }
  )
  .then(response => response.json())
  .then(data => {

    if (!timerEditing) {
  document.getElementById("timerInput").value =
    Number(data.timer);
}

    updateModeDisplay(data.mode);
    

    relayBtn.classList.toggle(
      "active",
      data.relay
    );
  })
  .catch(() => {});
}

// ==================================================
// BLEへ切替
// ==================================================

function switchBle() {

  fetch(
    "/switch_ble",
    {
      method: "POST",
      cache: "no-store"
    }
  );

  document.body.innerHTML =
    "<div style='color:white;background:#050505;" +
    "font-family:sans-serif;text-align:center;" +
    "padding-top:80px;'>" +
    "<h1>BLEモードへ切り替えます</h1>" +
    "<p>ATOMS3Rが自動再起動します。</p>" +
    "</div>";
}


// 初期表示
updateModeDisplay("DIRECT");
loadStatus();
setInterval(loadStatus, 300);

</script>

</body>
</html>
)rawliteral";

  return html;
}

void startWiFiMode() {
  blinkWhiteTwice();
  uint64_t chipid = ESP.getEfuseMac();
  char id[7];
  sprintf(id, "%06X", (uint32_t)(chipid & 0xFFFFFF));
  ssidName = "AtomRelay_" + String(id);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ssidName.c_str(), password);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  request->send(
    200,
    "text/html",
    makeRootHtml()
  );
});

server.on("/set_timer", HTTP_POST, [](AsyncWebServerRequest *request) {

  if (!request->hasParam("seconds")) {
    request->send(
      400,
      "text/plain",
      "NO_SECONDS"
    );
    return;
  }

  unsigned long seconds =
    request->getParam("seconds")->value().toInt();

  if (seconds < 1) {
    request->send(
      400,
      "text/plain",
      "BAD_SECONDS"
    );
    return;
  }

  timerDuration = seconds * 1000UL;

 // 現在のエンコーダ位置を新しい基準にする
  lastEncoderValue = encoder.getEncoderValue();

  timerSavePending = true;
  timerLastChangedTime = millis();

  if (currentMode == TIMER_MODE && !timerRunning) {
    drawScreen();
  }

  request->send(
    200,
    "text/plain",
    "TIMER_OK"
  );
});

// 本体の現在状態をブラウザへ返す
server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {

  if (!wifiClientConnected) {
  wifiClientConnected = true;
  drawScreen();
}

wifiLastAccessTime = millis();

String json = "{";

  json += "\"mode\":\"";
  json += modeToString(currentMode);
  json += "\"";

  json += ",\"timer\":";
  json += String(timerDuration / 1000UL);

  json += ",\"relay\":";
  json += relayState ? "true" : "false";

  json += "}";

  request->send(
    200,
    "application/json",
    json
  );
});

  server.on("/press", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (currentMode == DIRECT_MODE) webHolding = true;
    else if (currentMode == LATCH_MODE) webToggleRequest = true;
    else if (currentMode == TIMER_MODE) webTimerRequest = true;
    request->send(200, "text/plain", "PRESS");
  });

  server.on("/release", HTTP_POST, [](AsyncWebServerRequest *request) {
    webHolding = false;
    request->send(200, "text/plain", "RELEASE");
  });

  server.on("/set_mode", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("value")) {
      request->send(400, "text/plain", "NO_VALUE");
      return;
    }
    String value = request->getParam("value")->value();
    value.toUpperCase();

    if (value == "DIRECT") browserMode = DIRECT_MODE;
    else if (value == "LATCH") browserMode = LATCH_MODE;
    else if (value == "TIMER") browserMode = TIMER_MODE;
    else {
      request->send(400, "text/plain", "BAD_MODE");
      return;
    }

    modeSource = SOURCE_BROWSER;
    requestedMode = browserMode;
    modeChangeRequested = true;
    request->send(200, "text/plain", "MODE_OK");
  });

  server.on("/switch_ble", HTTP_POST, [](AsyncWebServerRequest *request) {
    saveCommMode(COMM_BLE);
    request->send(200, "text/plain", "SWITCHING_TO_BLE");
    requestRestart();
  });

  server.begin();
}

// ==================================================
// BLE コールバックと開始
// ==================================================
class RelayCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) {
    String value = characteristic->getValue().c_str();
    value.trim();
    value.toUpperCase();

    if (value == "PRESS") {
      if (currentMode == DIRECT_MODE) bleHolding = true;
      else if (currentMode == LATCH_MODE) bleToggleRequest = true;
      else if (currentMode == TIMER_MODE) bleTimerRequest = true;
    }
    else if (value == "RELEASE") {
      bleHolding = false;
    }
    else if (value == "MODE:DIRECT") {
      browserMode = DIRECT_MODE;
      modeSource = SOURCE_BROWSER;
      requestedMode = browserMode;
      modeChangeRequested = true;
    }
    else if (value == "MODE:LATCH") {
      browserMode = LATCH_MODE;
      modeSource = SOURCE_BROWSER;
      requestedMode = browserMode;
      modeChangeRequested = true;
    }
    else if (value == "MODE:TIMER") {
      browserMode = TIMER_MODE;
      modeSource = SOURCE_BROWSER;
      requestedMode = browserMode;
      modeChangeRequested = true;
    }
    else if (value == "WIFI") {
  saveCommMode(COMM_WIFI);
  requestRestart();
}

else if (value.startsWith("TIME:")) {
  unsigned long seconds =
    value.substring(5).toInt();

  if (seconds >= 1) {
    timerDuration = seconds * 1000UL;

    // 現在のエンコーダ位置を新しい基準にする
    lastEncoderValue =
      encoder.getEncoderValue();

    timerSavePending = true;
    timerLastChangedTime = millis();

    if (currentMode == TIMER_MODE &&
        !timerRunning) {
      drawScreen();
    }

    sendBleStatus();
  }
}

else if (value == "STATUS") {
  sendBleStatus();
}
  }
};

class ServerCallback : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    bleClientConnected = true;
    drawScreen();
  }

  void onDisconnect(BLEServer *pServer) override {
    bleClientConnected = false;
    drawScreen();

    BLEDevice::startAdvertising();
  }
};


void startBLEMode() {
  blinkBlueTwice();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);

  BLEDevice::init(BLE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallback());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new RelayCallback());
  pCharacteristic->setValue("READY");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

// ==================================================
// setup
// ==================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // 【ここがポイント】起動時にスプライトの画面バッファを確保
  spr.createSprite(M5.Display.width(), M5.Display.height());

  Wire.begin(38, 39);
  encoder.begin(&Wire, ENCODER_ADDR, 38, 39);

  M5.Display.setBrightness(100);
  M5.Display.setRotation(0);

  Serial.begin(115200);
  preferences.begin("relay", false);
  timerDuration = preferences.getULong("timer", 2000);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(INPUT_BUTTON_PIN, INPUT_PULLUP);

  currentMode = loadOperationMode();
  modeSource = SOURCE_PHYSICAL;
  relayState = false;
  timerRunning = false;

  commMode = loadCommMode();
  if (commMode == COMM_BLE) {
    startBLEMode();
  } else {
    startWiFiMode();
  }

  drawScreen();
}


// ==================================================
// loop
// ==================================================
void loop() {
  M5.update();

  if (modeChangeRequested) {
    modeChangeRequested = false;
    timerRunning = false;
    relayOff();
    applyModeChange(requestedMode);
  }

  if (currentMode == TIMER_MODE && !timerRunning) {
    int16_t currentEncoderValue = encoder.getEncoderValue();
    int rawDiff = currentEncoderValue - lastEncoderValue;
    int clicks = rawDiff / 2;

    if (clicks != 0) {
      lastEncoderValue += clicks * 2;

      int direction = clicks > 0 ? 1 : -1;
      int count = abs(clicks);

      for (int i = 0; i < count; i++) {
        unsigned long step;

        if (direction > 0) {
          if (timerDuration < 60000) step = 1000;
          else if (timerDuration < 300000) step = 15000;
          else if (timerDuration < 600000) step = 30000;
          else if (timerDuration < 3600000) step = 60000;
          else step = 300000;

          timerDuration += step;
        }
        else {
          if (timerDuration <= 60000) step = 1000;
          else if (timerDuration <= 300000) step = 15000;
          else if (timerDuration <= 600000) step = 30000;
          else if (timerDuration <= 3600000) step = 60000;
          else step = 300000;

          if (timerDuration > step) {
            timerDuration -= step;
          }
          else {
            timerDuration = 1000;
          }
        }
      }

      drawScreen();
      sendBleStatus();

      timerSavePending = true;
      timerLastChangedTime = millis();
    }
  }

  updateModeButton();

  bool physicalPressed =
  M5.BtnA.isPressed() ||
  digitalRead(INPUT_BUTTON_PIN) == LOW;

  // 10秒長押し処理
  if (physicalPressed) {
    if (atomHoldStart == 0) {
      atomHoldStart = millis();
    }

    if (!atomHoldSwitchDone &&
        millis() - atomHoldStart >= 10000) {

      atomHoldSwitchDone = true;

      if (commMode == COMM_WIFI) {
        saveCommMode(COMM_BLE);
        blinkBlueTwice();
      }
      else {
        saveCommMode(COMM_WIFI);
        blinkWhiteTwice();
      }

      requestRestart();
    }
  }
  else {
    atomHoldStart = 0;
    atomHoldSwitchDone = false;
  }

  // モード別処理
  if (currentMode == DIRECT_MODE) {
    timerRunning = false;

    bool remoteHolding =
      (commMode == COMM_WIFI)
        ? webHolding
        : bleHolding;

    if (physicalPressed || remoteHolding) {
      relayOn();
    }
    else {
      relayOff();
    }
  }

  else if (currentMode == LATCH_MODE) {
    timerRunning = false;

    if (physicalPressed && !lastPhysicalPressed) {
      if (relayState) {
        relayOff();
      }
      else {
        relayOn();
      }
    }

    if (commMode == COMM_WIFI && webToggleRequest) {
      webToggleRequest = false;

      if (relayState) {
        relayOff();
      }
      else {
        relayOn();
      }
    }

    if (commMode == COMM_BLE && bleToggleRequest) {
      bleToggleRequest = false;

      if (relayState) {
        relayOff();
      }
      else {
        relayOn();
      }
    }
  }

  else if (currentMode == TIMER_MODE) {
    bool startTimer = false;

    if (physicalPressed && !lastPhysicalPressed) {
      startTimer = true;
    }

    if (commMode == COMM_WIFI && webTimerRequest) {
      webTimerRequest = false;
      startTimer = true;
    }

    if (commMode == COMM_BLE && bleTimerRequest) {
      bleTimerRequest = false;
      startTimer = true;
    }

    if (startTimer && !timerRunning) {
      timerRunning = true;
      timerStartTime = millis();
      lastDisplayedSeconds = -1;
      relayOn();
    }

    if (timerRunning) {
      updateTimerCountdown();

      if (millis() - timerStartTime >= timerDuration) {
        timerRunning = false;
        relayOff();
      }
    }
  }

  lastPhysicalPressed = physicalPressed;

  if (timerSavePending &&
      millis() - timerLastChangedTime >= TIMER_SAVE_DELAY_MS) {

    preferences.putULong("timer", timerDuration);
    timerSavePending = false;
  }

  if (restartRequested &&
      millis() - restartRequestTime >= 800) {

    ESP.restart();
  }

  // Wi-Fi接続表示のタイムアウト（5秒）
  if (wifiClientConnected &&
      millis() - wifiLastAccessTime > 5000) {

    wifiClientConnected = false;
    drawScreen();
  }

  delay(1);
}

