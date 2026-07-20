#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

// ==================================================
// 設定
// ==================================================

const int RELAY_PIN = 2;

const unsigned long PAIR_HOLD_MS = 4000;
const unsigned long PAIR_WAIT_MS = 15000;

// ==================================================
// Wi-Fi (SSID・パスワード)
// ==================================================

String apSsid;
const char* AP_PASS = "12345678";

WebServer server(80);

// ==================================================
// BLE
// ==================================================

#define SERVICE_UUID        "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define CHARACTERISTIC_UUID "BEB5483E-36E1-4688-B7F5-EA07361B26A8"

BLECharacteristic *pCharacteristic = nullptr;
BLEServer *pServer = nullptr;
bool bleConnected = false;

// HTML (省略なしでそのまま維持)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <title>コンパクト無線スイッチ(Wi-Fi)</title>
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
      font-size: 26px;
      font-weight: 600;
      letter-spacing: 1px;
      margin: 0 0 16px;
      color: #fff;
    }
    .connectionModeRow {
      display: flex;
      gap: 10px;
      justify-content: center;
      margin-bottom: 24px;
    }
    .connBtn {
      flex: 1;
      max-width: 160px;
      font-size: 13px;
      font-weight: bold;
      padding: 10px 14px;
      border-radius: 20px;
      border: 2px solid #333;
      background: #111;
      color: #888;
      cursor: pointer;
      transition: all 0.2s;
    }
    .connBtn.active-wifi {
    border-color:#34c759;
    color:#fff;
    background:#052e14;

    box-shadow:
        0 0 10px rgba(52,199,89,.45),
        0 0 22px rgba(52,199,89,.30),
        inset 0 0 10px rgba(52,199,89,.15);
}
    .connBtn.active-ble {
      border-color: #007aff;
      color: #fff;
      background: #001a33;
      box-shadow: 0 0 10px rgba(0, 122, 255, 0.3);
    }
    .connBtn:active {
      transform: scale(0.96);
    }
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
      box-shadow: 0 0 16px rgba(255, 59, 48, 0.8), 0 0 35px rgba(255, 59, 48, 0.4);
    }
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
    .timerConfig {
      display: none;
      margin: 24px auto 12px;
      animation: fadeIn 0.3s ease-out;
    }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(-10px); }
      to { opacity: 1; transform: translateY(0); }
    }
/* --- タイマー入力欄のレイアウト修正 --- */
    .timerInputWrapper {
      display: inline-flex;
      align-items: center;
      background: #16161a;
      border: 1px solid #2c2c35;
      padding: 2px 6px;
      border-radius: 24px;
      margin-right: 12px;
      vertical-align: middle;
      user-select: none;
      -webkit-user-select: none;
    }
    /* 三角（矢印）ボタンのスタイル */
    .stepBtn {
      background: transparent !important; /* オレンジの背景を強制クリア */
      border: none !important;
      color: #ff9500 !important;
      font-size: 14px; /* 三角の大きさを少し控えめに */
      font-weight: bold;
      width: 28px;  /* ボタンの横幅 */
      height: 28px; /* ボタンの縦幅 */
      padding: 0 !important; /* 余計な広がりをカット */
      border-radius: 0 !important; /* 丸みをカット */
      box-shadow: none !important; /* 光る影をカット */
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      touch-action: none;
      vertical-align: middle;
    }
    .stepBtn:active {
      opacity: 0.5;
    }
    .timerInputWrapper input {
      font-size: 20px;
      font-weight: bold;
      padding: 4px 0;
      width: 55px;
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
      margin-left: 2px;
      padding-right: 6px;
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
      color: #aaa;
      margin-top: 32px;
      font-family: monospace;
      letter-spacing: 0.5px;
      background: #111;
      padding: 10px;
      border-radius: 8px;
      border: 1px solid #222;
      word-break: break-all;
    }

/* --- レシーバー情報インジケーター --- */
    /* --- レシーバー情報インジケーター（枠なし・コンパクト版） --- */
    .receiver-info-box {
      margin-top: 30px;
      padding: 0;
      background: transparent !important; /* 後ろの枠（背景）を完全に消去 */
      border: none !important;            /* 枠線を消去 */
      box-shadow: none !important;        /* 影を消去 */
      text-align: center;
    }
    .receiver-title {
      font-size: 10px;
      color: #444; /* やや暗めにして主役（機器名）を引き立てる */
      text-transform: uppercase;
      letter-spacing: 2px;
      margin-bottom: 2px; /* 隙間を詰める */
    }
    .receiver-device {
      font-size: 14px; /* 少しだけ小さくスマートに */
      color: #aaa;     /* パキッとした白から、馴染むグレーへ変更 */
      font-weight: bold;
      font-family: 'Courier New', Courier, monospace;
      letter-spacing: 1px;
      margin-bottom: 10px; /* バッジとの間隔 */
    }
    .status-badge-container {
      display: flex;
      justify-content: center;
      gap: 10px;
      font-size: 12px;
    }
    .status-badge {
      padding: 3px 10px;
      border-radius: 20px;
      background: #222;
      color: #444;
      font-weight: bold;
      transition: all 0.3s ease;
      border: 1px solid transparent;
    }
    /* アクティブ状態の光る演出 */
    .status-badge.active-wifi {
  background: rgba(52, 199, 89, 0.10);
  color: #34c759;
  border-color: rgba(52, 199, 89, 0.40);
  text-shadow: 0 0 8px rgba(52, 199, 89, 0.60);
  box-shadow: 0 0 8px rgba(52, 199, 89, 0.20);
}
    .status-badge.active-ble {
      background: rgba(0, 255, 149, 0.1);
      color: #00ff95;
      border-color: rgba(0, 255, 149, 0.4);
      text-shadow: 0 0 8px rgba(0, 255, 149, 0.6);
      box-shadow: 0 0 8px rgba(0, 255, 149, 0.2);
    }

  </style>
</head>
<body>
  <div class="wrap">
    <h1>コンパクト無線スイッチ(Wi-Fi)</h1>
    <div class="connectionModeRow">
  <button id="bleConnectBtn" class="connBtn"
    onclick="connectBLE()">
    BLE接続
  </button>
</div>
    <button id="relayBtn" class="mainBtn">ON</button>
    <div class="modeRow">
      <button id="directBtn" class="modeBtn" onclick="selectMode('DIRECT')">ダイレクト</button>
      <button id="latchBtn" class="modeBtn" onclick="selectMode('LATCH')">ラッチ</button>
      <button id="timerBtn" class="modeBtn" onclick="selectMode('TIMER')">タイマー</button>
    </div>
    <!-- 変更後：三角ボタン付きタイマーパネル -->
    <div id="timerPanel" class="timerConfig">
      <div class="timerInputWrapper">
        <button type="button" class="stepBtn" id="btnDown">▼</button>
        <input type="number" id="timerInput" min="1" value="2">
        <button type="button" class="stepBtn" id="btnUp">▲</button>
        <span class="unit">秒</span>
      </div>
      <button onclick="commitTimerTime()">設定する</button>
   
 </div>

<p id="status">接続準備中</p>

<!-- レシーバー情報インジケーター表示エリア -->
<div class="receiver-info-box">
      <div class="receiver-title">Connected Receiver</div>
      <div class="receiver-device">M5Stack AtomS3 Lite</div>
      <div class="status-badge-container">
        <!-- ボタンの名称と完全一致させて直感的にする -->
        <span id="badgeWifi" class="status-badge active-wifi">Wi-Fi </span>
        <span id="badgeBle" class="status-badge">BLE</span>
      </div>
        </div>

    <!-- Wi-Fi名設定 -->
    <div style="margin-top:40px; padding-top:24px; border-top:1px solid #222;">
      <div style="font-size:14px; color:#aaa; margin-bottom:12px;">
        Wi-Fi/BLE接続先の名前
      </div>

      <input
        type="text"
        id="wifiNameInput"
        maxlength="31"
        placeholder="例：スイッチ１"
        style="
          width:80%;
          max-width:320px;
          padding:12px;
          font-size:16px;
          color:white;
          background:#111;
          border:1px solid #333;
          border-radius:10px;
          outline:none;
          box-sizing:border-box;
        "
      >

      <button
        id="wifiNameSaveBtn"
        style="
          display:block;
          margin:14px auto 0;
          padding:11px 24px;
          font-size:15px;
          font-weight:bold;
          color:white;
          background:#1c1c1e;
          border:1px solid #444;
          border-radius:20px;
          cursor:pointer;
        "
      >
        Wi-Fi/BLE名を保存
      </button>
    </div>

  </div>

  <script>
    const SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
    const CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
let communicationMode =
  window.location.hostname === "192.168.4.1"
    ? "wifi"
    : "ble";    


    let bleDevice = null;
    
    let bleServer = null;
    let characteristic = null;
    let currentMode = "DIRECT";
    let timerEditing = false;
    let latchRelayOn = false;
    let timerActiveTimeout = null;
    const relayBtn = document.getElementById("relayBtn");
    const timerInput = document.getElementById("timerInput");
    const bleConnectBtn =
  document.getElementById("bleConnectBtn");
    const statusText = document.getElementById("status");

    const wifiNameInput = document.getElementById("wifiNameInput");
const wifiNameSaveBtn = document.getElementById("wifiNameSaveBtn");

wifiNameSaveBtn.addEventListener("click", async () => {

  const name = wifiNameInput.value.trim();

  if (name.length === 0) {
    alert("Wi-Fi名を入力してください");
    return;
  }

  try {
    const res = await fetch("/setssid?name=" + encodeURIComponent(name));

    if (res.ok) {
      alert("保存しました。\n再起動後に反映されます。");
    } else {
      alert("保存できませんでした。");
    }

  } catch (e) {
    alert("通信できませんでした。");
  }

});

   window.addEventListener("DOMContentLoaded", () => {
  const badgeWifi = document.getElementById("badgeWifi");
  const badgeBle = document.getElementById("badgeBle");

  if (communicationMode === "wifi") {
    badgeWifi.classList.add("active-wifi");
    badgeBle.classList.remove("active-ble");

    bleConnectBtn.style.display = "inline-block";
bleConnectBtn.textContent = "Wi-Fi未接続";
bleConnectBtn.disabled = true;

    updateStatus("Wi-Fi接続確認中...");
    fetchStatusWifi();

    // M5StickS3側の変更をブラウザへ追従させる
    setInterval(fetchStatusWifi, 300);
  } else {
    badgeWifi.classList.remove("active-wifi");
    badgeBle.classList.remove("active-ble");

    bleConnectBtn.style.display = "inline-block";

    updateStatus("BLE未接続");
  }

  updateModeDisplay("DIRECT");

    fetch("/getssid")
    .then(res => res.text())
    .then(name => {
      wifiNameInput.value = name;
    })
    .catch(() => {});
});

    

    async function sendCommand(text) {
      updateStatus("TX: " + text);
      if (communicationMode === "wifi") {
        try {
          let url = "";
          if (text === "PRESS") url = "/press";
          else if (text === "RELEASE") url = "/release";
          else if (text.startsWith("MODE:")) url = "/mode?val=" + text.split(":")[1];
          else if (text.startsWith("TIME:")) url = "/time?val=" + text.split(":")[1];
          else if (text === "STATUS") url = "/status";
          else return;
          const response = await fetch(url);
          if (response.ok) {
            const resText = await response.text();
            if (resText.startsWith("STATUS:")) applyStatusText(resText);
          } else {
            updateStatus("ERR: Wi-Fi送信失敗 (" + response.status + ")");
          }
        } catch (error) {
          updateStatus("ERR: Wi-Fi通信不可");
        }
      } else if (communicationMode === "ble") {
        if (!characteristic) {
          const ok = await connectBLE();
          if (!ok) return;
        }
        const ready = await reconnectIfNeeded();
        if (!ready || !characteristic) return;
        try {
          const encoder = new TextEncoder();
          await characteristic.writeValue(encoder.encode(text));
        } catch (error) {
          updateStatus("ERR: BLE送信失敗");
        }
      }
    }

    async function fetchStatusWifi() {
      try {
        const response = await fetch("/status");
        if (response.ok) {

    bleConnectBtn.textContent = "Wi-Fi接続済み";

    bleConnectBtn.classList.add("active-wifi");

    const resText = await response.text();
    applyStatusText(resText);
}
      } catch (e) {

    bleConnectBtn.textContent = "Wi-Fi未接続";

    bleConnectBtn.classList.remove("active-wifi");

}
    }


    function updateStatus(msg) {
      statusText.textContent = msg;
    }

    async function connectBLE() {
      try {
        updateStatus("STATUS: BLEデバイス探索中...");
      
        bleDevice = await navigator.bluetooth.requestDevice({
  filters: [{ services: [SERVICE_UUID] }]
});
        bleDevice.addEventListener("gattserverdisconnected", handleDisconnected);
        updateStatus("STATUS: BLE接続中...");
        bleServer = await bleDevice.gatt.connect();
        const service = await bleServer.getPrimaryService(SERVICE_UUID);
        characteristic = await service.getCharacteristic(CHARACTERISTIC_UUID);
        await enableNotifications();
        updateStatus("STATUS: BLE接続完了");
document
  .getElementById("badgeBle")
  .classList.add("active-ble");

bleConnectBtn.textContent = "BLE接続済み";
        setTimeout(() => { sendCommand("STATUS"); }, 500);
        return true;
      } catch (error) {
        updateStatus("ERROR: " + error);
        return false;
      }
    }

    async function reconnectIfNeeded() {
      if (!bleDevice) return false;
      if (bleDevice.gatt.connected && characteristic) return true;
      try {
        bleServer = await bleDevice.gatt.connect();
        const service = await bleServer.getPrimaryService(SERVICE_UUID);
        characteristic = await service.getCharacteristic(CHARACTERISTIC_UUID);
        await enableNotifications();
        return true;
      } catch (error) {
        return false;
      }
    }

    async function enableNotifications() {
      await characteristic.startNotifications();
      characteristic.removeEventListener("characteristicvaluechanged", handleBleNotification);
      characteristic.addEventListener("characteristicvaluechanged", handleBleNotification);
    }

    function handleBleNotification(event) {
      const text = new TextDecoder("utf-8").decode(event.target.value);
      applyStatusText(text);
    }

    function handleDisconnected() {
      characteristic = null;
      bleServer = null;
      updateStatus("STATUS: BLE切断");
document
  .getElementById("badgeBle")
  .classList.remove("active-ble");

bleConnectBtn.textContent = "BLE接続";
      if (timerActiveTimeout !== null) {
        clearTimeout(timerActiveTimeout);
        timerActiveTimeout = null;
      }
      relayBtn.classList.remove("active");
    }

    function applyStatusText(text) {
      if (!text.startsWith("STATUS:")) return;
      const mode = text.match(/MODE:([A-Z]+)/);
      const relay = text.match(/RELAY:(ON|OFF)/);
      const timer = text.match(/TIME:(\d+)/);
      if (mode) {
        const newMode = mode[1].toUpperCase();
        if (currentMode !== newMode) {
          updateModeDisplay(newMode);
          if (newMode !== "TIMER" && timerActiveTimeout !== null) {
            clearTimeout(timerActiveTimeout);
            timerActiveTimeout = null;
          }
        }
      }
      if (relay) {
        const isOn = (relay[1] === "ON");
        relayBtn.classList.toggle("active", isOn);
        if (currentMode === "LATCH") latchRelayOn = isOn;
      }
      if (timer && !timerEditing) {
        timerInput.value = Number(timer[1]);
      }
      updateStatus("RX: " + text);
    }

    timerInput.addEventListener("focus", () => { timerEditing = true; });
    timerInput.addEventListener("input", () => { timerEditing = true; });
    timerInput.addEventListener("blur", () => { setTimeout(() => { timerEditing = false; }, 300); });

    function updateModeDisplay(mode) {
      currentMode = mode.trim().toUpperCase();
      const directBtn = document.getElementById("directBtn");
      const latchBtn = document.getElementById("latchBtn");
      const timerBtn = document.getElementById("timerBtn");
      const timerPanel = document.getElementById("timerPanel");
      directBtn.classList.remove("directActive");
      latchBtn.classList.remove("latchActive");
      timerBtn.classList.remove("timerActive");
      if (currentMode === "DIRECT") {
        directBtn.classList.add("directActive");
        timerPanel.style.display = "none";
      } else if (currentMode === "LATCH") {
        latchBtn.classList.add("latchActive");
        timerPanel.style.display = "none";
      } else if (currentMode === "TIMER") {
        timerBtn.classList.add("timerActive");
        timerPanel.style.display = "block";
      }
    }

    async function pressRelay(event) {
      event.preventDefault();
      if (currentMode === "DIRECT") {
        relayBtn.classList.add("active");
      } else if (currentMode === "LATCH") {
        latchRelayOn = !latchRelayOn;
        relayBtn.classList.toggle("active", latchRelayOn);
      } else if (currentMode === "TIMER") {
        relayBtn.classList.add("active");
        if (timerActiveTimeout !== null) clearTimeout(timerActiveTimeout);
        const seconds = Number(timerInput.value);
        timerActiveTimeout = setTimeout(() => {
          relayBtn.classList.remove("active");
          timerActiveTimeout = null;
        }, seconds * 1000);
      }
      await sendCommand("PRESS");
    }

    async function releaseRelay(event) {
      event.preventDefault();
      if (currentMode === "DIRECT") relayBtn.classList.remove("active");
      await sendCommand("RELEASE");
    }

    relayBtn.addEventListener("pointerdown", pressRelay);
    relayBtn.addEventListener("pointerup", releaseRelay);
    relayBtn.addEventListener("pointercancel", releaseRelay);
    relayBtn.addEventListener("pointerleave", releaseRelay);

    async function selectMode(mode) {
      updateModeDisplay(mode);
      relayBtn.classList.remove("active");
      latchRelayOn = false;
      if (timerActiveTimeout !== null) {
        clearTimeout(timerActiveTimeout);
        timerActiveTimeout = null;
      }
      updateStatus("STATUS: Mode " + mode);
      await sendCommand("MODE:" + mode);
      setTimeout(() => { sendCommand("STATUS"); }, 300);
    }

    async function commitTimerTime() {
      const seconds = Number(timerInput.value);
      if (!seconds || seconds < 1) {
        alert("1秒以上を入力してください");
        return;
      }
      timerEditing = true;
      updateStatus("STATUS: Timer " + seconds + " sec");
      await sendCommand("TIME:" + seconds);
      timerEditing = false;
      setTimeout(() => { sendCommand("STATUS"); }, 300);
    }

// --- 三角ボタンの長押し・高速化スクリプト ---
    const btnUp = document.getElementById("btnUp");
    const btnDown = document.getElementById("btnDown");
    let holdTimer = null;
    let holdInterval = null;

    function doStep(amount) {
      let val = Number(timerInput.value) + amount;
      if (val < 1) val = 1; // 1秒未満にならないよう制限
      timerInput.value = val;
      timerEditing = true;
    }

    function startHold(amount) {
      doStep(amount);
      // 最初は0.4秒後に連続変化を開始
      holdTimer = setTimeout(() => {
        // 最初は0.15秒間隔で変化
        holdInterval = setInterval(() => {
          doStep(amount);
        }, 150);

        // さらに1.2秒押し続けたら、0.05秒間隔の「超高速変化」に切り替え
        holdTimer = setTimeout(() => {
          clearInterval(holdInterval);
          holdInterval = setInterval(() => {
            doStep(amount);
          }, 50);
        }, 1200);

      }, 400);
    }

    function stopHold() {
      clearTimeout(holdTimer);
      clearInterval(holdInterval);
      
    }

    // 上向き三角 (▲) のイベント割り当て
    btnUp.addEventListener("pointerdown", (e) => { e.preventDefault(); startHold(1); });
    btnUp.addEventListener("pointerup", stopHold);
    btnUp.addEventListener("pointercancel", stopHold);
    btnUp.addEventListener("pointerleave", stopHold);

    // 下向き三角 (▼) のイベント割り当て
    btnDown.addEventListener("pointerdown", (e) => { e.preventDefault(); startHold(-1); });
    btnDown.addEventListener("pointerup", stopHold);
    btnDown.addEventListener("pointercancel", stopHold);
    btnDown.addEventListener("pointerleave", stopHold);

  </script>
</body>
</html>
)rawliteral";

// ==================================================
// 通信データ
// ==================================================

enum MessageType : uint8_t {
  MSG_PAIR_REQUEST = 1,
  MSG_PAIR_RESPONSE = 2,
  MSG_RELAY_COMMAND = 3,
  MSG_CONNECTION_CHECK = 4,
  MSG_MODE_UPDATE = 5,
  MSG_STATE_SYNC = 6
};

enum RelayMode : uint8_t {
  MODE_DIRECT = 0,
  MODE_TIMER = 1,
  MODE_LATCH = 2
};

enum ButtonAction : uint8_t {
  ACTION_PRESS = 1,
  ACTION_RELEASE = 2
};

struct EspNowPacket {
  uint8_t messageType;
  uint8_t mode;
  uint8_t action;
  uint16_t timerSeconds;
  uint8_t relayState;
  char deviceName[32];
};

// ==================================================
// 状態
// ==================================================

Preferences preferences;

uint8_t controllerMac[6];
bool controllerRegistered = false;

bool pairingMode = false;
unsigned long pairingStartTime = 0;
unsigned long pairingBlinkTime = 0;
bool pairingBlinkOn = false;

bool pairingSuccessLed = false;
unsigned long pairingSuccessLedStart = 0;
bool relayState = false;
bool timerRunning = false;
unsigned long timerEndTime = 0;
uint16_t webTimerSeconds = 2;

RelayMode currentMode = MODE_DIRECT;

// ==================================================
// LED表示
// ==================================================

const int RGB_LED_PIN = 35;
const int RGB_LED_COUNT = 1;
const uint8_t LED_BRIGHTNESS = 18;

Adafruit_NeoPixel rgbLed(
  RGB_LED_COUNT,
  RGB_LED_PIN,
  NEO_GRB + NEO_KHZ800
);

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
  rgbLed.setPixelColor(0, rgbLed.Color(red, green, blue));
  rgbLed.show();
}

void showModeLed() {
  switch (currentMode) {
    case MODE_DIRECT:
      setLedColor(0, 38, 90);
      break;
    case MODE_TIMER:
      setLedColor(85, 55, 0);
      break;
    case MODE_LATCH:
      setLedColor(0, 50, 15);
      break;
  }
}

void showInputLed() {
  setLedColor(255, 0, 0);
}

// ==================================================
// リレー制御
// ==================================================

void setRelay(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);

  if (on) {
    showInputLed();
  } else {
    showModeLed();
  }

  Serial.print("Relay: ");
  Serial.println(on ? "ON" : "OFF");
}

// ==================================================
// Wi-Fi・BLE共通の状態文字列
// ==================================================

String makeStatusText() {
  String modeText;

  switch (currentMode) {
    case MODE_DIRECT:
      modeText = "DIRECT";
      break;

    case MODE_TIMER:
      modeText = "TIMER";
      break;

    case MODE_LATCH:
      modeText = "LATCH";
      break;
  }

  return
    "STATUS:MODE:" + modeText +
    ",RELAY:" + String(relayState ? "ON" : "OFF") +
    ",TIME:" + String(webTimerSeconds);
}


// ==================================================
// BLEへ状態通知
// ==================================================

void notifyBleStatus() {
  if (!bleConnected || pCharacteristic == nullptr) {
    return;
  }

  String status = makeStatusText();

  pCharacteristic->setValue(status.c_str());
  pCharacteristic->notify();
}


// ==================================================
// BLEから受け取った命令を実行
// ==================================================

void executeBleCommand(String command) {
  command.trim();

  if (command.startsWith("NAME:")) {
    String newName = command.substring(5);
    newName.trim();

    if (newName.length() >= 1 && newName.length() <= 31) {
      preferences.putString("apName", newName);

      pCharacteristic->setValue("NAME_SAVED");
      pCharacteristic->notify();

      delay(500);
      ESP.restart();
    }

    return;
  }

  command.toUpperCase();

  if (command == "PRESS") {

    switch (currentMode) {
      case MODE_DIRECT:
        timerRunning = false;
        setRelay(true);
        break;

      case MODE_TIMER:
        setRelay(true);
        timerRunning = true;
        timerEndTime =
          millis() +
          static_cast<unsigned long>(webTimerSeconds) * 1000UL;
        break;

      case MODE_LATCH:
        timerRunning = false;
        setRelay(!relayState);
        break;
    }
    sendStateSync();
  }

  else if (command == "RELEASE") {
  if (currentMode == MODE_DIRECT) {
    setRelay(false);
    sendStateSync();
  }
}

  else if (command.startsWith("MODE:")) {
    String modeText = command.substring(5);

    timerRunning = false;
    setRelay(false);

    if (modeText == "DIRECT") {
      currentMode = MODE_DIRECT;
    }
    else if (modeText == "TIMER") {
      currentMode = MODE_TIMER;
    }
    else if (modeText == "LATCH") {
      currentMode = MODE_LATCH;
    }

    showModeLed();
sendStateSync();
  }

  else if (command.startsWith("TIME:")) {
    long seconds = command.substring(5).toInt();

    if (seconds < 1) {
      seconds = 1;
    }

    if (seconds > 65535) {
      seconds = 65535;
    }

    webTimerSeconds = static_cast<uint16_t>(seconds);
    sendStateSync();
  }

  else if (command == "STATUS") {
    // 状態通知だけ行う
  }

  notifyBleStatus();
}


// ==================================================
// BLEコールバック
// ==================================================

class RelayBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    Serial.println("BLE connected");

    notifyBleStatus();
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("BLE disconnected");

    // 切断後も再接続できるよう広告を再開
    BLEDevice::startAdvertising();
  }
};


class RelayBleCharacteristicCallbacks
  : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *characteristic) override {
    String command = characteristic->getValue();

    if (command.length() == 0) {
      return;
    }

    Serial.print("BLE RX: ");
    Serial.println(command);

    executeBleCommand(command);
  }
};

// ==================================================
// MACアドレス表示
// ==================================================

void printMac(const uint8_t *mac) {
  Serial.printf(
    "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2],
    mac[3], mac[4], mac[5]
  );
}

// ==================================================
// ESP-NOW送信先登録
// ==================================================

bool addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  return esp_now_add_peer(&peerInfo) == ESP_OK;
}

// ==================================================
// コントローラー保存
// ==================================================

void saveController(const uint8_t *mac) {
  memcpy(controllerMac, mac, 6);
  controllerRegistered = true;

  preferences.putBytes("controller", controllerMac, 6);

  Serial.print("Controller saved: ");
  printMac(controllerMac);
  Serial.println();
}

void loadController() {
  if (preferences.getBytesLength("controller") == 6) {
    preferences.getBytes("controller", controllerMac, 6);
    controllerRegistered = true;

    addPeer(controllerMac);

    Serial.print("Controller loaded: ");
    printMac(controllerMac);
    Serial.println();
  }
}

// ==================================================
// ペアリング応答
// ==================================================

void sendPairResponse(const uint8_t *destinationMac) {
  if (!addPeer(destinationMac)) {
    Serial.println("Peer registration failed");
    return;
  }

  EspNowPacket packet = {};
  packet.messageType = MSG_PAIR_RESPONSE;
  packet.mode = 1;
  strlcpy(packet.deviceName, apSsid.c_str(), sizeof(packet.deviceName));

  esp_err_t result = esp_now_send(
    destinationMac,
    reinterpret_cast<uint8_t *>(&packet),
    sizeof(packet)
  );

  if (result == ESP_OK) {
    Serial.println("Pair response sent");
  } else {
    Serial.println("Pair response failed");
  }
}

// ==================================================
// M5StickSへ現在の状態を送信
// ==================================================

void sendStateSync() {

  if (!controllerRegistered) return;

  EspNowPacket packet = {};

  packet.messageType = MSG_STATE_SYNC;
  strlcpy(packet.deviceName, apSsid.c_str(), sizeof(packet.deviceName));
  packet.mode = currentMode;
  packet.timerSeconds = webTimerSeconds;
  packet.relayState = relayState ? 1 : 0;

  esp_now_send(controllerMac,
               (uint8_t *)&packet,
               sizeof(packet));
}

// ==================================================
// ESP-NOW受信
// ==================================================

// 引数の型を環境に合わせて互換性を持たせるため、const uint8_t* で受け取る形に修正します
void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data,
               int len) {

  if (len != sizeof(EspNowPacket)) {
    return;
  }

  EspNowPacket packet;
  memcpy(&packet, data, sizeof(packet));

  // 引数 info から送信元MACアドレスを取得
  const uint8_t *senderMac = info->src_addr;

  if (packet.messageType == MSG_PAIR_REQUEST) {
    if (!pairingMode) {
      return;
    }
    saveController(senderMac);
sendPairResponse(senderMac);
pairingMode = false;

setLedColor(0, 255, 0);
pairingSuccessLed = true;
pairingSuccessLedStart = millis();

Serial.println("Pairing completed");
return;
  }

  if (!controllerRegistered) {
    return;
  }

  if (memcmp(senderMac, controllerMac, 6) != 0) {
    return;
  }

  if (packet.messageType == MSG_MODE_UPDATE) {
  currentMode = static_cast<RelayMode>(packet.mode);
  webTimerSeconds = packet.timerSeconds;

  if (!relayState) {
    showModeLed();
  }

  sendStateSync();
  notifyBleStatus();

  return;
}

  if (packet.messageType == MSG_CONNECTION_CHECK) {
    return;
  }

  if (packet.messageType != MSG_RELAY_COMMAND) {
    return;
  }

  currentMode = static_cast<RelayMode>(packet.mode);

  if (!relayState) {
    showModeLed();
  }

    switch (currentMode) {
    case MODE_DIRECT:
      timerRunning = false;
      if (packet.action == ACTION_PRESS) setRelay(true);
      if (packet.action == ACTION_RELEASE) setRelay(false);
      break;

    case MODE_TIMER:
      if (packet.action == ACTION_PRESS) {
        setRelay(true);
        timerRunning = true;
        timerEndTime =
          millis() +
          static_cast<unsigned long>(packet.timerSeconds) * 1000UL;
      }
      break;

    case MODE_LATCH:
      if (packet.action == ACTION_PRESS) {
        timerRunning = false;
        setRelay(!relayState);
      }
      break;
  }

  sendStateSync();
  notifyBleStatus();
}


// ==================================================
// 初期設定
// ==================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);

  rgbLed.begin();
  rgbLed.setBrightness(LED_BRIGHTNESS);
  rgbLed.clear();
  rgbLed.show();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  relayState = false;

  showModeLed();

  // ==================================================
  // Wi-Fi安定化設定
  // ・周囲のWi-Fiスキャンをしない
  // ・家庭用ルーターへ接続しない
  // ・固定チャネルで本機専用APだけを起動
  // ・省電力スリープを無効化
  // ・同時接続は1台だけ
  // ==================================================
   preferences.begin("relayRx", false);
  
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);   // ESP-NOWとの共存に必要
  WiFi.setSleep(false);
  uint64_t chipid = ESP.getEfuseMac();
uint32_t id = (uint32_t)(chipid & 0xFFFFFF);

char buf[32];
sprintf(buf, "AtomRelay_%06X", id);

String defaultName = String(buf);

apSsid = preferences.getString("apName", defaultName);

  if (!WiFi.softAP(apSsid.c_str(), AP_PASS)) {
  Serial.println("Wi-Fi AP start failed");
} else {
  Serial.printf(
    "AP Started: %s / IP: %s\n",
    apSsid.c_str(),
    WiFi.softAPIP().toString().c_str()
  );
}

  // ESP-NOW初期化
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    while (true) {
      delay(1000);
    }
  }

  esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(onReceive));

 
  loadController();

  Serial.println("Receiver Ready");

  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/press", handlePress);
  server.on("/release", handleRelease);
  server.on("/mode", handleMode);
  server.on("/time", handleTime);
  server.on("/status", handleStatus);

  server.on("/setssid", []() {

    if (!server.hasArg("name")) {
      server.send(400, "text/plain", "SSID name missing");
      return;
    }

    String newName = server.arg("name");
    newName.trim();

    if (newName.length() == 0 || newName.length() > 31) {
      server.send(400, "text/plain", "Invalid SSID name");
      return;
    }

    preferences.putString("apName", newName);

server.send(
  200,
  "text/plain",
  "Saved. Restarting..."
);

delay(500);
ESP.restart();

  });

  server.on("/setname", HTTP_GET, []() {

    String newName = server.arg("val");
    newName.trim();

    if (newName.length() < 1 || newName.length() > 31) {
      server.send(400, "text/plain", "Invalid name");
      return;
    }

    preferences.putString("apName", newName);

    server.send(
      200,
      "text/plain",
      "Saved. Restarting..."
    );

    delay(500);
    ESP.restart();
  });

  server.on("/getssid", []() {
    server.send(200, "text/plain", apSsid);
  });

  server.begin();

// ==================================================
// BLEサーバー開始
// ==================================================

BLEDevice::init(apSsid.c_str());

pServer = BLEDevice::createServer();
pServer->setCallbacks(new RelayBleServerCallbacks());

BLEService *bleService =
  pServer->createService(SERVICE_UUID);

pCharacteristic =
  bleService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

pCharacteristic->setCallbacks(
  new RelayBleCharacteristicCallbacks()
);

pCharacteristic->addDescriptor(new BLE2902());

pCharacteristic->setValue(makeStatusText().c_str());

bleService->start();

BLEAdvertising *advertising =
  BLEDevice::getAdvertising();

advertising->addServiceUUID(SERVICE_UUID);
advertising->setScanResponse(true);

BLEDevice::startAdvertising();

Serial.println("BLE advertising started");


}

// ==================================================
// メイン処理
// ==================================================

void loop() {
  M5.update();

  // 本体ボタン長押し4秒でペアリング待機
  if (M5.BtnA.pressedFor(PAIR_HOLD_MS) && !pairingMode) {
  pairingMode = true;
  

  pairingBlinkTime = millis();
  pairingBlinkOn = true;
  setLedColor(0, 0, 255);

  Serial.println("Pairing mode active: 15 seconds");

    while (M5.BtnA.isPressed()) {
      M5.update();
      delay(10);
    }
    pairingStartTime = millis();
  }

  // ペアリング待機中は青色点滅
if (pairingMode &&
    millis() - pairingBlinkTime >= 500) {

  pairingBlinkTime = millis();
  pairingBlinkOn = !pairingBlinkOn;

  if (pairingBlinkOn) {
    setLedColor(0, 0, 255);
  } else {
    setLedColor(0, 0, 0);
  }
}

  // ペアリング成功時の緑色を300ミリ秒後に元へ戻す
  if (pairingSuccessLed &&
      millis() - pairingSuccessLedStart >= 300) {

    pairingSuccessLed = false;
    showModeLed();
  }

  // ペアリングタイムアウト
 if (pairingMode && millis() - pairingStartTime >= PAIR_WAIT_MS) {
  pairingMode = false;

  setLedColor(255, 0, 0);
  delay(1000);
  showModeLed();

  Serial.println("Pairing timeout");
}

  // タイマー終了
  if (timerRunning &&
    static_cast<long>(millis() - timerEndTime) >= 0) {

  timerRunning = false;
  setRelay(false);
  // タイマー終了
if (timerRunning &&
    static_cast<long>(millis() - timerEndTime) >= 0) {

  timerRunning = false;
  setRelay(false);
  sendStateSync();
  notifyBleStatus();
}
  notifyBleStatus();
}

  delay(10);
  server.handleClient();
}

// ==================================================
// WebServer 各種ハンドラ
// ==================================================

void handlePress() {
  switch (currentMode) {
    case MODE_DIRECT:
      timerRunning = false;
      setRelay(true);
      break;
    case MODE_TIMER:
      setRelay(true);
      timerRunning = true;
      timerEndTime = millis() + static_cast<unsigned long>(webTimerSeconds) * 1000UL;
      break;
    case MODE_LATCH:
      timerRunning = false;
      setRelay(!relayState);
      break;
  }
    sendStateSync();
  handleStatus();

}

void handleRelease() {
  if (currentMode == MODE_DIRECT) {
    setRelay(false);
    sendStateSync();
  }

  handleStatus();
}

void handleMode() {
  if (!server.hasArg("val")) {
    server.send(400, "text/plain", "MODE value missing");
    return;
  }

  String modeText = server.arg("val");
  modeText.toUpperCase();

  timerRunning = false;
  setRelay(false);

  if (modeText == "DIRECT") {
    currentMode = MODE_DIRECT;
  }
  else if (modeText == "TIMER") {
    currentMode = MODE_TIMER;
  }
  else if (modeText == "LATCH") {
    currentMode = MODE_LATCH;
  }
  else {
    server.send(400, "text/plain", "Unknown mode");
    return;
  }

  showModeLed();
sendStateSync();
handleStatus();
}

void handleTime() {
  if (!server.hasArg("val")) {
    server.send(400, "text/plain", "TIME value missing");
    return;
  }

  long seconds = server.arg("val").toInt();
  if (seconds < 1) {
    server.send(400, "text/plain", "Time must be 1 or more");
    return;
  }

  if (seconds > 65535) {
    seconds = 65535;
  }

  webTimerSeconds = static_cast<uint16_t>(seconds);
  sendStateSync();
  handleStatus();
}

void handleStatus() {
  String status = makeStatusText();

  server.send(200, "text/plain", status);
}

 
