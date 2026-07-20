#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>

// ==================================================
// 基本設定
// ==================================================

const int MAX_RECEIVERS = 5;

// 長押し時間
const unsigned long TIMER_SETTING_HOLD_MS = 1000;

// タイマー設定中の操作
const unsigned long TIMER_REPEAT_START_MS = 500;  // 0.5秒後から連続変更
const unsigned long TIMER_REPEAT_INTERVAL_MS = 100; // 100msごと
const unsigned long TIMER_SETTING_TIMEOUT_MS = 3000; // 3秒無操作で終了

unsigned long lastTimerSettingActivity = 0;
unsigned long lastTimerRepeatTime = 0;

const unsigned long PAIRING_HOLD_MS = 4000;
const unsigned long TARGET_SELECT_HOLD_MS = 2000;

// ペアリング待機時間
const unsigned long PAIRING_WAIT_MS = 15000;
const unsigned long PAIR_REQUEST_INTERVAL_MS = 500;



// ==================================================
// 通信データ
// ==================================================
#define GRAY 0x7BEF
#define LIGHTGRAY 0xC618 // 明るいグレー
#define SOFT_PINK 0xFD16 // やさしいピンク
#define PANEL 0x1082     // 濃いグレーのカード背景
#define PANEL_LINE 0x2945 // 控えめなカード枠
#define SOFT_GREEN 0x07EA // やさしい緑

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
// 保存データ・状態
// ==================================================

Preferences preferences;

const int EXTERNAL_KEY1_PIN_G5 = 5;
const int EXTERNAL_KEY1_PIN_G7 = 7;

bool key1IsPressed() {
  return M5.BtnA.isPressed() ||
         digitalRead(EXTERNAL_KEY1_PIN_G5) == LOW ||
         digitalRead(EXTERNAL_KEY1_PIN_G7) == LOW;
}

uint8_t receiverMacs[MAX_RECEIVERS][6];
uint8_t receiverTypes[MAX_RECEIVERS];  // 受信機の種類
char receiverNames[MAX_RECEIVERS][32];
int receiverCount = 0;
int activeReceiver = 0;

RelayMode currentMode = MODE_DIRECT;

// タイマー時間
uint16_t timerSeconds = 5;

// 各画面の状態
bool timerSettingMode = false;
bool targetSelectMode = false;
bool pairingMode = false;
bool deleteConfirmMode = false;
int deleteTargetIndex = -1;


// ペアリング用
unsigned long pairingStartTime = 0;
unsigned long lastPairRequestTime = 0;

// ペアリングで見つかった機器を、確認後に登録するための一時保存
volatile bool pairResponsePending = false;

bool pairConfirmMode = false;
bool pairConfirmButtonsReleased = false;

uint8_t pendingReceiverMac[6] = {};
uint8_t pendingReceiverType = 0;
char pendingReceiverName[32] = {};

// 長押し処理済み判定
bool combinationHandled = false;
bool key1LongHandled = false;
bool key2LongHandled = false;
// ESP-NOW送信結果
volatile bool sendResultReady = false;
volatile bool sendSucceeded = false;
enum SendPurpose : uint8_t {
  SEND_NONE = 0,
  SEND_RELAY = 1,
  SEND_CONNECTION_CHECK = 2
};

volatile SendPurpose pendingSendPurpose = SEND_NONE;
volatile SendPurpose completedSendPurpose = SEND_NONE;

// カウントダウン用
unsigned long timerStartMillis = 0;
bool isTimerCounting = false;
bool relayOn = false;

volatile bool stateSyncDisplayPending = false;

// 選択中の受信機と直近で通信できたか
bool receiverReachable = false;

// 接続結果画面をキー入力まで表示
bool connectionNoticeWaiting = false;
bool connectionNoticeButtonsReleased = false;

// ブロードキャストMAC
uint8_t broadcastMac[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

// 接続機器の日本語名
const char* receiverName(uint8_t type) {
  if (type == 1) {
    return "AtomS3Lite";
  }
  if (type == 2) {
    return "AtomS3U";
  }
  return "Atom端末";
}

// ==================================================
// モード名（日本語）
// ==================================================

const char* modeNameJa() {
  switch (currentMode) {
    case MODE_DIRECT:
      return "ダイレクト";
    case MODE_TIMER:
      return "タイマー";
    case MODE_LATCH:
      return "ラッチ";
  }
  return "";
}


// ==================================================
// UI共通部品
// ==================================================

enum ScreenType : uint8_t {
  SCREEN_NORMAL,
  SCREEN_TIMER,
  SCREEN_TARGET,
  SCREEN_NOTICE
};

ScreenType currentScreen = SCREEN_NORMAL;
int lastBatteryLevel = -1;
bool lastChargingState = false;
unsigned long lastBatteryRefresh = 0;

uint16_t modeColor() {
  if (currentMode == MODE_DIRECT) return CYAN;
  if (currentMode == MODE_TIMER) return ORANGE;
  return SOFT_GREEN;
}

void drawBatteryStatus(bool force = false) {
  int level = constrain(M5.Power.getBatteryLevel(), 0, 100);
  bool charging = M5.Power.isCharging();

  if (!force && level == lastBatteryLevel && charging == lastChargingState) return;

  lastBatteryLevel = level;
  lastChargingState = charging;

  const bool compactHeader =
  (currentScreen == SCREEN_NORMAL || currentScreen == SCREEN_TARGET);
  const int rowY = compactHeader ? 2 : 25;
  const int batteryY = compactHeader ? 7 : 30;
  const int textY = compactHeader ? 4 : 27;

  // 電池表示行だけ消して再描画
  M5.Display.fillRect(0, rowY, M5.Display.width(), 23, BLACK);

  const int rightMargin = 5;
  const int batteryW = 25;
  const int batteryH = 12;

  String percentText = String(level) + "%";
  M5.Display.setFont(&fonts::efontJA_24);
  int percentW = M5.Display.textWidth(percentText.c_str());

  // ％表示を画面右端から配置し、その左へ電池と稲妻を並べる
  const int percentX = M5.Display.width() - rightMargin - percentW;
  const int batteryX = percentX - batteryW - 7;
  const int boltX = batteryX - 13;

  if (charging) {
    M5.Display.fillTriangle(
      boltX + 5, batteryY - 3,
      boltX + 12, batteryY - 3,
      boltX + 7, batteryY + 5,
      YELLOW
    );
    M5.Display.fillTriangle(
      boltX + 7, batteryY + 3,
      boltX + 13, batteryY + 3,
      boltX + 4, batteryY + 13,
      YELLOW
    );
  }

  M5.Display.drawRoundRect(
    batteryX, batteryY, batteryW, batteryH, 2, WHITE
  );
  M5.Display.fillRect(
    batteryX + batteryW, batteryY + 3, 3, 6, WHITE
  );

  uint16_t fillColor = charging ? YELLOW :
                       (level <= 20 ? SOFT_PINK :
                       (level <= 50 ? ORANGE : SOFT_GREEN));

  int fillWidth = map(level, 0, 100, 0, batteryW - 4);
  if (fillWidth > 0) {
    M5.Display.fillRect(
      batteryX + 2,
      batteryY + 2,
      fillWidth,
      batteryH - 4,
      fillColor
    );
  }

  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setCursor(percentX, textY);
  M5.Display.print(percentText);






if (relayOn) {

  // ONバッジ
  const uint16_t ON_BG = 0xFDD8;   // 薄いピンク

  int badgeX = 4;
  int badgeY = textY - 4;

  int badgeWidth = 42;     // 今より細く
  int badgeHeight = 22;     // 少し低く
  int cornerRadius = 11;    // 高さの半分くらい＝カプセル型

  // 背景
  M5.Display.fillRoundRect(
      badgeX,
      badgeY,
      badgeWidth,
      badgeHeight,
      cornerRadius,
      ON_BG);

  // 文字
M5.Display.setFont(&fonts::efontJA_24);
M5.Display.setTextColor(0x8000);

int textW = M5.Display.textWidth("ON");

int textX = badgeX + (badgeWidth - textW) / 2;
int textY = badgeY;

// 疑似太字
M5.Display.setCursor(textX, textY);
M5.Display.print("ON");

M5.Display.setCursor(textX + 1, textY);
M5.Display.print("ON");

M5.Display.setCursor(textX, textY + 1);
M5.Display.print("ON");

M5.Display.setCursor(textX + 1, textY + 1);
M5.Display.print("ON");




}

}

void refreshBatteryIfNeeded() {
  if (millis() - lastBatteryRefresh < 250) return;
  lastBatteryRefresh = millis();
  drawBatteryStatus(false);
}

void drawBaseLayout(const char* title, uint16_t accentColor) {
  M5.Display.setRotation(0);
  M5.Display.setTextWrap(false);
  M5.Display.fillScreen(BLACK);

  // 通常画面はタイトルを置かず、電池表示を右上に配置
  if (title[0] != '\0') {
    M5.Display.setFont(&fonts::efontJA_24);
    M5.Display.setTextColor(WHITE);
    int titleX = (M5.Display.width() - M5.Display.textWidth(title)) / 2;
    M5.Display.setCursor(titleX, 1);
    M5.Display.print(title);
  }

  drawBatteryStatus(true);

  int lineY = (title[0] == '\0') ? 28 : 50;
  M5.Display.fillRoundRect(
    8, lineY, M5.Display.width() - 16, 5, 2, accentColor
  );
}

void drawCenteredBoldText(const char* text, int y, uint16_t color) {
  M5.Display.setFont(&fonts::efontJA_24);
  M5.Display.setTextColor(color);

  int textW = M5.Display.textWidth(text);

  // 右にはみ出しやすいため、1pxだけ左へ補正
  int x = (M5.Display.width() - textW) / 2 - 1;

  // 疑似太字
  M5.Display.setCursor(x, y);
  M5.Display.print(text);

  M5.Display.setCursor(x + 1, y);
  M5.Display.print(text);
}

void drawCenteredMessage(const char* text, int y, uint16_t color) {
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(color);
  int x = (M5.Display.width() - M5.Display.textWidth(text)) / 2;
  M5.Display.setCursor(x, y);
  M5.Display.print(text);
}

void drawCenteredMessageLarge(const char* text, int y, uint16_t color) {
  M5.Display.setFont(&fonts::efontJA_24);
  M5.Display.setTextColor(color);
  int x = (M5.Display.width() - M5.Display.textWidth(text)) / 2;
  M5.Display.setCursor(x, y);
  M5.Display.print(text);
}

// 2行表示
void drawBottomHelp(
  const char* line1,
  const char* line2,
  uint16_t accentColor
) {
  const int y = 177;
  const int h = 56;

  M5.Display.fillRoundRect(
    4, y, M5.Display.width() - 8, h, 8, PANEL
  );
  M5.Display.drawRoundRect(
    4, y, M5.Display.width() - 8, h, 8, accentColor
  );

  drawCenteredMessage(line1, y + 5, WHITE);
  drawCenteredMessage(line2, y + 29, WHITE);
}


// 3行表示
void drawBottomHelp(
  const char* line1,
  const char* line2,
  const char* line3,
  uint16_t accentColor
) {
  const int y = 165;
  const int h = 68;

  M5.Display.fillRoundRect(
    4, y, M5.Display.width() - 8, h, 8, PANEL
  );
  M5.Display.drawRoundRect(
    4, y, M5.Display.width() - 8, h, 8, accentColor
  );

  drawCenteredMessage(line1, y + 3, WHITE);
  drawCenteredMessage(line2, y + 24, WHITE);
  drawCenteredMessage(line3, y + 45, WHITE);
}
// ==================================================
// 通常画面
// ==================================================

void drawNormalScreen() {
  currentScreen = SCREEN_NORMAL;
  uint16_t accent = modeColor();
  drawBaseLayout("", accent);

  // タイトルを削除した分、通常画面全体を上へ詰める
  M5.Display.fillRoundRect(4, 38, M5.Display.width() - 8, 100, 10, PANEL);
  M5.Display.drawRoundRect(4, 38, M5.Display.width() - 8, 100, 10, accent);

  drawCenteredMessage("現在のモード", 45, LIGHTGRAY);
  drawCenteredBoldText(modeNameJa(), 70, accent);

if (currentMode == MODE_TIMER) {

  if (isTimerCounting) {

    // 残り秒数を計算
    long elapsed = (millis() - timerStartMillis) / 1000;
    long remaining = (long)timerSeconds - elapsed;

    if (remaining < 0) {
      remaining = 0;
    }

    String timerText = "残り ";

    int minutes = remaining / 60;
    int seconds = remaining % 60;

    if (minutes == 0) {
      timerText += String(seconds) + "秒";
    } else if (seconds == 0) {
      timerText += String(minutes) + "分";
    } else {
      timerText += String(minutes) + "分" + String(seconds) + "秒";
    }

    drawCenteredMessageLarge(
      timerText.c_str(),
      104,
      SOFT_PINK
    );

  } else {

    String timerText;

    int minutes = timerSeconds / 60;
    int seconds = timerSeconds % 60;

    if (minutes == 0) {
      timerText = String(seconds) + "秒";
    } else if (seconds == 0) {
      timerText = String(minutes) + "分";
    } else {
      timerText =
        String(minutes) + "分" +
        String(seconds) + "秒";
    }

    drawCenteredMessageLarge(
      timerText.c_str(),
      104,
      WHITE
    );
  }
}

  bool connected = receiverCount > 0 && receiverReachable;

const char* devName = "未接続";

if (connected) {
  if (strlen(receiverNames[activeReceiver]) > 0) {
    devName = receiverNames[activeReceiver];
  } else {
    devName = receiverName(receiverTypes[activeReceiver]);
  }
}

  M5.Display.fillRoundRect(4, 152, M5.Display.width() - 8, 55, 8, PANEL);
  M5.Display.drawRoundRect(4, 152, M5.Display.width() - 8, 55, 8, PANEL_LINE);

  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(LIGHTGRAY);
  M5.Display.setCursor(11, 157);
  M5.Display.print("接続先");

  M5.Display.fillCircle(15, 188, 5,
                        connected ? SOFT_GREEN : LIGHTGRAY);
  M5.Display.setTextColor(connected ? WHITE : LIGHTGRAY);
  M5.Display.setCursor(26, 176);
  M5.Display.print(devName);
}

// ==================================================
// 時間設定画面
// ==================================================

void drawTimerSettingScreen() {
  currentScreen = SCREEN_TIMER;
  drawBaseLayout("タイマー設定", ORANGE);

  const int centerX = M5.Display.width() / 2;

  M5.Display.fillRoundRect(4, 60, M5.Display.width() - 8, 112, 10, PANEL);
  M5.Display.drawRoundRect(4, 60, M5.Display.width() - 8, 112, 10, ORANGE);

  M5.Display.fillTriangle(centerX, 69,
                          centerX - 13, 82,
                          centerX + 13, 82,
                          ORANGE);

  String timeText;

int minutes = timerSeconds / 60;
int seconds = timerSeconds % 60;

if (minutes == 0) {
  timeText = String(seconds) + "秒";
} else if (seconds == 0) {
  timeText = String(minutes) + "分";
} else {
  timeText = String(minutes) + "分" + String(seconds) + "秒";
}

M5.Display.setFont(&fonts::efontJA_24);
M5.Display.setTextColor(WHITE);

int textWidth = M5.Display.textWidth(timeText.c_str());
int startX = centerX - textWidth / 2;

// 疑似太字
M5.Display.setCursor(startX + 1, 101);
M5.Display.print(timeText);

M5.Display.setCursor(startX, 101);
M5.Display.print(timeText);

  M5.Display.fillTriangle(centerX, 162,
                          centerX - 13, 149,
                          centerX + 13, 149,
                          ORANGE);

  drawBottomHelp("KEY1：＋1秒", "KEY2：－1秒", ORANGE);
}

// ==================================================
// 通信先選択画面
// ==================================================

void drawTargetScreen() {
  currentScreen = SCREEN_TARGET;
  drawBaseLayout("", CYAN);

  if (receiverCount == 0) {
   M5.Display.fillRoundRect(4, 60, M5.Display.width() - 8, 173, 10, PANEL);
    M5.Display.drawRoundRect(4, 60, M5.Display.width() - 8, 173, 10, SOFT_PINK);
    drawCenteredMessageLarge("デバイスなし", 126, SOFT_PINK);
    return;
  }

  M5.Display.fillRoundRect(4, 38, M5.Display.width() - 8, 112, 10, PANEL);
M5.Display.drawRoundRect(4, 38, M5.Display.width() - 8, 112, 10, CYAN);
  M5.Display.setFont(&fonts::efontJA_24);
  M5.Display.setTextColor(CYAN);
  M5.Display.setCursor(10, 79);
M5.Display.print("<");
M5.Display.setCursor(105, 79);
M5.Display.print(">");

  const char* devName;

if (strlen(receiverNames[activeReceiver]) > 0) {
  devName = receiverNames[activeReceiver];
} else {
  devName = receiverName(receiverTypes[activeReceiver]);
}
  drawCenteredMessageLarge(devName, 55, WHITE);

  String countText = String(activeReceiver + 1) + " / " + String(receiverCount);
  drawCenteredMessageLarge(countText.c_str(), 107, LIGHTGRAY);

 drawBottomHelp(
  "KEY1：次へ",
  "KEY2：決定",
  "KEY1長：新規",
  CYAN
);
}

// ==================================================
// ペアリング・通知画面
// ==================================================

void drawPairingScreen(const char* message) {
  currentScreen = SCREEN_NOTICE;
  M5.Display.setRotation(0);
  M5.Display.setTextWrap(false);
  M5.Display.fillScreen(BLACK);

  int w = M5.Display.width();
  uint16_t themeColor = SOFT_GREEN;
  const char* title = "通信状態";
  const char* main1 = message;
  const char* main2 = "";
  const char* sub1 = "";
  const char* sub2 = "";

  String msgStr = String(message);
  if (msgStr.equals("PAIRING...")) {
    themeColor = CYAN;
    title = "ペアリング中";
    main1 = "探索中・・";
  } else if (msgStr.equals("PAIRING OK")) {
    themeColor = SOFT_GREEN;
    title = "接続完了";
    main1 = "ペアリング完了";

  } else if (msgStr.equals("REGISTER?")) {
  themeColor = CYAN;
  title = "新規機器を発見";
  main1 = pendingReceiverName;
  main2 = "登録しますか？";
  sub1 = "KEY2：登録";
  sub2 = "KEY1：NO";


  } else if (msgStr.equals("PAIR TIMEOUT")) {
    themeColor = SOFT_PINK;
    title = "ペアリング結果";
    main1 = "相手が";
    main2 = "見つかりません";
    sub1 = "受信機の電源を";
    sub2 = "確認してください";
  } else if (msgStr.equals("NO RECEIVER")) {
    themeColor = SOFT_PINK;
    title = "接続先";
    main1 = "接続先が";
    main2 = "ありません";
  } else if (msgStr.equals("ESP-NOW ERROR")) {
    themeColor = SOFT_PINK;
    title = "通信エラー";
    main1 = "初期化";
    main2 = "できません";


  } else if (msgStr.equals("CONNECTION OK")) {
    themeColor = SOFT_GREEN;
    title = "接続結果";
    main1 = "接続";
    main2 = "成功しました";
    sub1 = "キーを押すと";
    sub2 = "通常画面へ";

  } else if (msgStr.equals("SEND FAILED")) {
  themeColor = SOFT_PINK;
  title = "接続結果";
  main1 = "接続";
  main2 = "できません";
  sub1 = "キーを押すと";
  sub2 = "通常画面へ";

  } else if (msgStr.equals("CHECKING...")) {
    themeColor = CYAN;
    title = "接続確認中";
    main1 = "少しお待ちください";


  }  else if (msgStr.equals("DELETE?")) {
  themeColor = SOFT_PINK;
  title = "登録機削除";

  main1 = "削除？";

  if (deleteTargetIndex >= 0 &&
      deleteTargetIndex < receiverCount &&
      strlen(receiverNames[deleteTargetIndex]) > 0) {

    main2 = receiverNames[deleteTargetIndex];

  } else {
    main2 = "対象機器";
  }

  sub1 = "KEY2長押：削除";
  sub2 = "KEY1：やめる";
}

else if (msgStr.equals("DELETED")) {
  themeColor = GREEN;
  title = "登録削除";
  sub1 = "KEYを押して戻る";
}
  

if (msgStr.equals("REGISTER?") ||
    msgStr.equals("PAIRING...")) {

  // 登録確認画面とペアリング中画面だけ16px
  M5.Display.setFont(&fonts::efontJA_16);

} else {

  // その他の画面は今までどおり24px
  M5.Display.setFont(&fonts::efontJA_24);
}

M5.Display.setTextColor(themeColor);
M5.Display.setCursor((w - M5.Display.textWidth(title)) / 2, 1);
M5.Display.print(title);

  drawBatteryStatus(true);
  M5.Display.fillRoundRect(8, 50, w - 16, 5, 2, themeColor);

M5.Display.fillRoundRect(4, 60, M5.Display.width() - 8, 173, 10, PANEL);
M5.Display.drawRoundRect(4, 60, M5.Display.width() - 8, 173, 10, themeColor);
  if (msgStr.equals("PAIRING...")) {

  // 「相手を探しています」だけ小さい文字で表示
  drawCenteredMessage(main1, 96, themeColor);

} else if (msgStr.equals("REGISTER?")) {

  // 登録確認画面
  drawCenteredMessage(main1, 82, themeColor);
  drawCenteredMessage(main2, 113, themeColor);

} else if (msgStr.equals("DELETE?")) {

  // 「削除？」は今までどおり大きく，機器名だけ小さくする
  drawCenteredMessageLarge(main1, 82, themeColor);
  drawCenteredMessage(main2, 113, themeColor);

} else if (strlen(main2) == 0) {

  // その他の画面は今までどおり
  drawCenteredMessageLarge(main1, 96, themeColor);

} else {

  // その他の画面は今までどおり
  drawCenteredMessageLarge(main1, 82, themeColor);
  drawCenteredMessageLarge(main2, 113, themeColor);
}

  if (strlen(sub1) > 0) {
    drawCenteredMessage(sub1, 162, WHITE);
    drawCenteredMessage(sub2, 187, WHITE);
  }

  if (msgStr.equals("PAIRING...") || msgStr.equals("CHECKING...")) {
    int cy = 182;
    for (int i = 0; i < 5; i++) {
      uint16_t c = (i == 2) ? themeColor : PANEL_LINE;
      M5.Display.fillCircle(44 + i * 10, cy, 4, c);
    }
  }
}

// ==================================================
// システム処理系（変更なし）
// ==================================================

void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  return esp_now_add_peer(&peerInfo) == ESP_OK;
}

void saveReceivers() {
  preferences.putInt("count", receiverCount);
  preferences.putInt("active", activeReceiver);
  preferences.putInt("timer", timerSeconds);
  preferences.putUChar("mode", currentMode);
  for (int i = 0; i < receiverCount; i++) {
  String key = "mac" + String(i);
  preferences.putBytes(key.c_str(), receiverMacs[i], 6);

  String typeKey = "type" + String(i);
  preferences.putUChar(typeKey.c_str(), receiverTypes[i]);

  String nameKey = "name" + String(i);
  preferences.putString(nameKey.c_str(), receiverNames[i]);
}
}

void deleteReceiver(int index) {

  if (index < 0 || index >= receiverCount) return;

  for (int i = index; i < receiverCount - 1; i++) {

    memcpy(receiverMacs[i], receiverMacs[i + 1], 6);

    receiverTypes[i] = receiverTypes[i + 1];

    strlcpy(receiverNames[i],
            receiverNames[i + 1],
            sizeof(receiverNames[i]));
  }

  receiverCount--;

  if (activeReceiver >= receiverCount) {
    activeReceiver = max(0, receiverCount - 1);
  }

  deleteConfirmMode = false;
  deleteTargetIndex = -1;

  saveReceivers();


}






void loadReceivers() {
  receiverCount = preferences.getInt("count", 0);
  activeReceiver = preferences.getInt("active", 0);
  timerSeconds = preferences.getInt("timer", 5);
  currentMode = static_cast<RelayMode>(preferences.getUChar("mode", MODE_DIRECT));

  if (receiverCount < 0 || receiverCount > MAX_RECEIVERS) receiverCount = 0;
  if (receiverCount == 0) activeReceiver = 0;
  else if (activeReceiver < 0 || activeReceiver >= receiverCount) activeReceiver = 0;
  if (timerSeconds < 1 || timerSeconds > 60) timerSeconds = 5;
  if (currentMode < MODE_DIRECT || currentMode > MODE_LATCH) currentMode = MODE_DIRECT;

for (int i = 0; i < receiverCount; i++) {
  String key = "mac" + String(i);

  if (preferences.getBytesLength(key.c_str()) == 6) {
    preferences.getBytes(key.c_str(), receiverMacs[i], 6);

    String typeKey = "type" + String(i);
    receiverTypes[i] = preferences.getUChar(typeKey.c_str(), 0);

    String nameKey = "name" + String(i);
    String savedName = preferences.getString(nameKey.c_str(), "");

    strlcpy(
      receiverNames[i],
      savedName.c_str(),
      sizeof(receiverNames[i])
    );

    addPeer(receiverMacs[i]);
  }
}
}

int findReceiver(const uint8_t* mac) {
  for (int i = 0; i < receiverCount; i++) {
    if (memcmp(receiverMacs[i], mac, 6) == 0) return i;
  }
  return -1;
}

void registerReceiver(
  const uint8_t* mac,
  uint8_t receiverType,
  const char* deviceName
) {
  int existingIndex = findReceiver(mac);

  if (existingIndex >= 0) {
    activeReceiver = existingIndex;
    receiverTypes[existingIndex] = receiverType;

    strlcpy(
      receiverNames[existingIndex],
      deviceName,
      sizeof(receiverNames[existingIndex])
    );

    saveReceivers();
    return;
  }

  if (receiverCount >= MAX_RECEIVERS) return;

  memcpy(receiverMacs[receiverCount], mac, 6);
  receiverTypes[receiverCount] = receiverType;

  strlcpy(
    receiverNames[receiverCount],
    deviceName,
    sizeof(receiverNames[receiverCount])
  );

  addPeer(receiverMacs[receiverCount]);

  activeReceiver = receiverCount;
  receiverCount++;

  saveReceivers();
}

// ==================================================
// ESP-NOW送信結果
// ==================================================

void onDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  // ペアリング探索用のブロードキャスト結果は画面表示に使わない
  if (pendingSendPurpose == SEND_NONE) return;

  completedSendPurpose = pendingSendPurpose;
  pendingSendPurpose = SEND_NONE;
  sendSucceeded = (status == ESP_NOW_SEND_SUCCESS);
  sendResultReady = true;
}

void sendRelayCommand(ButtonAction action) {
  if (receiverCount == 0) {
    drawPairingScreen("NO RECEIVER");
    delay(700);
    drawNormalScreen();
    return;
  }
  EspNowPacket packet = {};
  packet.messageType = MSG_RELAY_COMMAND;
  packet.mode = currentMode;
  packet.action = action;
  packet.timerSeconds = timerSeconds;

  pendingSendPurpose = SEND_RELAY;
  esp_err_t result = esp_now_send(
    receiverMacs[activeReceiver],
    reinterpret_cast<uint8_t*>(&packet),
    sizeof(packet)
  );

  // 送信要求自体を受け付けられなかった場合
  if (result != ESP_OK) {
    pendingSendPurpose = SEND_NONE;
    sendSucceeded = false;
    sendResultReady = true;
  }
}

void sendConnectionCheck() {
  if (receiverCount == 0) {
    drawPairingScreen("NO RECEIVER");
    delay(700);
    drawNormalScreen();
    return;
  }

  EspNowPacket packet = {};
  packet.messageType = MSG_CONNECTION_CHECK;

  pendingSendPurpose = SEND_CONNECTION_CHECK;
  esp_err_t result = esp_now_send(
    receiverMacs[activeReceiver],
    reinterpret_cast<uint8_t*>(&packet),
    sizeof(packet)
  );

  if (result != ESP_OK) {
    pendingSendPurpose = SEND_NONE;
    completedSendPurpose = SEND_CONNECTION_CHECK;
    sendSucceeded = false;
    sendResultReady = true;
  }
}

// モード変更をレシーバーへ即時通知
void sendModeUpdate() {
  if (receiverCount == 0) return;

  EspNowPacket packet = {};
  packet.messageType = MSG_MODE_UPDATE;
  packet.mode = currentMode;
  packet.timerSeconds = timerSeconds;

  // モード表示更新は補助通信なので、失敗表示の対象にはしない
  esp_now_send(
    receiverMacs[activeReceiver],
    reinterpret_cast<uint8_t*>(&packet),
    sizeof(packet)
  );
}

void sendPairRequest() {
  EspNowPacket packet = {};
  packet.messageType = MSG_PAIR_REQUEST;
  esp_now_send(broadcastMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
}

void onReceive(const esp_now_recv_info_t* info,
               const uint8_t* data,
               int len) {

  if (len != sizeof(EspNowPacket)) return;

  EspNowPacket packet;
  memcpy(&packet, data, sizeof(packet));

  // --------------------------------------------------
  // ペアリング応答
  // --------------------------------------------------
  if (packet.messageType == MSG_PAIR_RESPONSE) {

  if (!pairingMode) return;

  // すでに登録済みの機器からの応答は、新規登録候補にしない
  if (findReceiver(info->src_addr) >= 0) {
    return;
  }

  // 見つかった機器を一時保存する
  memcpy(pendingReceiverMac, info->src_addr, 6);
  pendingReceiverType = packet.mode;

  strlcpy(
    pendingReceiverName,
    packet.deviceName,
    sizeof(pendingReceiverName)
  );

  // 名前が空だった場合の表示名
  if (strlen(pendingReceiverName) == 0) {
    strlcpy(
      pendingReceiverName,
      receiverName(pendingReceiverType),
      sizeof(pendingReceiverName)
    );
  }

  // ここではまだ登録しない
  pairingMode = false;
  pairResponsePending = true;

  return;
}

  // --------------------------------------------------
  // 状態同期
  // --------------------------------------------------
  if (packet.messageType == MSG_STATE_SYNC) {

    int index = findReceiver(info->src_addr);

if (index >= 0 && strlen(packet.deviceName) > 0) {
  strlcpy(
    receiverNames[index],
    packet.deviceName,
    sizeof(receiverNames[index])
  );
  saveReceivers();
}


  currentMode = static_cast<RelayMode>(packet.mode);
  timerSeconds = packet.timerSeconds;

  relayOn = packet.relayState;

  if (currentMode == MODE_TIMER && packet.relayState) {
    if (!isTimerCounting) {
      timerStartMillis = millis();
      isTimerCounting = true;
    }
  } else if (!packet.relayState) {
    isTimerCounting = false;
  }


stateSyncDisplayPending = true;

  return;
}
}


// ==================================================
// 初期設定
// ==================================================

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  pinMode(EXTERNAL_KEY1_PIN_G5, INPUT_PULLUP);
  pinMode(EXTERNAL_KEY1_PIN_G7, INPUT_PULLUP);


  M5.Display.setRotation(0); // 縦向きに設定
  M5.Display.fillScreen(BLACK);
  M5.Display.setBrightness(80);
 
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    drawPairingScreen("ESP-NOW ERROR");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onReceive);

  esp_now_register_send_cb(onDataSent);

  addPeer(broadcastMac);
  preferences.begin("relayTx", false);
  loadReceivers();
  drawNormalScreen();
 
}


bool key1StableState = false;
bool key1LastRawState = false;
bool key1PressedEvent = false;
bool key1ReleasedEvent = false;

unsigned long key1LastChangeTime = 0;
unsigned long key1PressedStartTime = 0;

const unsigned long KEY1_DEBOUNCE_MS = 30;


void updateKey1() {
  bool rawState = key1IsPressed();

  key1PressedEvent = false;
  key1ReleasedEvent = false;

  if (rawState != key1LastRawState) {
    key1LastRawState = rawState;
    key1LastChangeTime = millis();
  }

  if (millis() - key1LastChangeTime >= KEY1_DEBOUNCE_MS) {
    if (rawState != key1StableState) {
      key1StableState = rawState;

      if (key1StableState) {
        key1PressedEvent = true;
        key1PressedStartTime = millis();
      } else {
        key1ReleasedEvent = true;
      }
    }
  }
}



// ==================================================
// メインループ
// ==================================================

void loop() {
  
  M5.update();
  updateKey1();

  refreshBatteryIfNeeded();

  if (stateSyncDisplayPending) {
  stateSyncDisplayPending = false;

  if (!timerSettingMode) {
    drawNormalScreen();
  }
}

  // ペアリング応答を受け取ったら登録確認画面を表示
  if (pairResponsePending) {
  pairResponsePending = false;

  pairingMode = false;

  pairConfirmMode = true;
  pairConfirmButtonsReleased = false;

    drawPairingScreen("REGISTER?");

    delay(10);
    return;
  }

  // ESP-NOW送信結果
  if (sendResultReady) {
    bool success = sendSucceeded;
    sendResultReady = false;

    SendPurpose finishedPurpose = completedSendPurpose;
    completedSendPurpose = SEND_NONE;

    if (success) {
      receiverReachable = true;

      if (finishedPurpose == SEND_CONNECTION_CHECK) {
        drawPairingScreen("CONNECTION OK");

        connectionNoticeWaiting = true;
        connectionNoticeButtonsReleased = false;

        sendModeUpdate();
      } else {
        drawNormalScreen();
      }

    } else {
      receiverReachable = false;

      drawPairingScreen("SEND FAILED");

      connectionNoticeWaiting = true;
      connectionNoticeButtonsReleased = false;
    }
  }

  bool key1Pressed = key1IsPressed();
  bool key2Pressed = M5.BtnB.isPressed();

  // 新しく見つかった機器を登録するか確認中
  if (pairConfirmMode) {

    // 確認画面が出る前に押していたキーを一度離す
    if (!key1Pressed && !key2Pressed) {
      pairConfirmButtonsReleased = true;
    }

    if (pairConfirmButtonsReleased) {

      // KEY1：登録せずに接続先選択へ戻る
      if (key1PressedEvent) {
        pairConfirmMode = false;
        pairConfirmButtonsReleased = false;

        targetSelectMode = true;

key1LongHandled = false;
key2LongHandled = false;
combinationHandled = false;

drawTargetScreen();

delay(10);
return;
      }

      // KEY2：登録する
      if (M5.BtnB.wasPressed()) {
        pairConfirmMode = false;
        pairConfirmButtonsReleased = false;

        registerReceiver(
          pendingReceiverMac,
          pendingReceiverType,
          pendingReceiverName
        );

        receiverReachable = true;

        drawPairingScreen("PAIRING OK");

        connectionNoticeWaiting = true;
        connectionNoticeButtonsReleased = false;

        sendModeUpdate();

        delay(10);
        return;
      }
    }

    delay(10);
    return;
  }

// 接続結果画面は，KEY1またはKEY2を押すまで表示する
if (connectionNoticeWaiting) {

  // KEY1を押し続けたままKEY2を押した場合は，
  // キーを押し直さなくても接続先選択画面へ入る
  if (key1StableState &&
      millis() - key1PressedStartTime >= TARGET_SELECT_HOLD_MS &&
      key2Pressed) {

    connectionNoticeWaiting = false;
    connectionNoticeButtonsReleased = false;

    combinationHandled = true;
    key1LongHandled = true;
    key2LongHandled = true;

    targetSelectMode = true;
    receiverReachable = false;
    deleteConfirmMode = false;
    deleteTargetIndex = -1;

    drawTargetScreen();

    // 接続先選択へ入るために押していたキーは，
    // 両方とも一度離すまで選択操作として読まない
    while (key1IsPressed() || M5.BtnB.isPressed()) {
      M5.update();
      updateKey1();
      delay(10);
    }

    M5.update();
    updateKey1();

 key1PressedEvent = false;
key1ReleasedEvent = false;

// KEY2を離したときのクリック判定をここで1回消費する
(void)M5.BtnB.wasClicked();

key1LongHandled = false;
key2LongHandled = false;
combinationHandled = false;

drawTargetScreen();

delay(50);
return;
  }

  // 接続結果画面が出る前に押していたキーを一度離す
  if (!key1Pressed && !key2Pressed) {
    connectionNoticeButtonsReleased = true;
  }

  // 一度キーを離した後，新たに押されたら通常画面へ戻る
  if (connectionNoticeButtonsReleased &&
      (key1PressedEvent || M5.BtnB.wasPressed())) {

    connectionNoticeWaiting = false;
    connectionNoticeButtonsReleased = false;

    drawNormalScreen();

    delay(10);
    return;
  }

  delay(10);
  return;
}

  if (pairingMode) {

  // 探索中にKEY1またはKEY2を押したら中止
  if (key1PressedEvent || M5.BtnB.wasPressed()) {
    pairingMode = false;
    receiverReachable = false;

    key1PressedEvent = false;
    key1ReleasedEvent = false;
    key1LongHandled = false;
    key2LongHandled = false;
    combinationHandled = false;

    targetSelectMode = true;
    drawTargetScreen();

    delay(50);
    return;
  }

  if (millis() - pairingStartTime >= PAIRING_WAIT_MS) {
    pairingMode = false;
    receiverReachable = false;

    drawPairingScreen("PAIR TIMEOUT");
    delay(800);

    targetSelectMode = true;
    drawTargetScreen();

    return;
  }
    if (millis() - lastPairRequestTime >= PAIR_REQUEST_INTERVAL_MS) {
      lastPairRequestTime = millis();
      sendPairRequest();
    }
    delay(10);
    return;
  }

// KEY1とKEY2を両方放したら、組み合わせ操作を再び受け付ける
if (!key1Pressed && !key2Pressed) {
  combinationHandled = false;
}

// KEY1を2秒以上押してから、KEY1を押したままKEY2を押す
if (!combinationHandled &&
    key1StableState &&
    millis() - key1PressedStartTime >= TARGET_SELECT_HOLD_MS &&
    key2Pressed) {

  combinationHandled = true;
  key1LongHandled = true;
  key2LongHandled = true;

 // ダイレクトモードではリレーOFFを送るが，
// 失敗しても接続エラー画面は出さない
if (currentMode == MODE_DIRECT && receiverCount > 0) {
  EspNowPacket packet = {};
  packet.messageType = MSG_RELAY_COMMAND;
  packet.mode = currentMode;
  packet.action = ACTION_RELEASE;
  packet.timerSeconds = timerSeconds;

  esp_now_send(
    receiverMacs[activeReceiver],
    reinterpret_cast<uint8_t*>(&packet),
    sizeof(packet)
  );
}
  targetSelectMode = true;
  receiverReachable = false;
  deleteConfirmMode = false;
  deleteTargetIndex = -1;

  drawTargetScreen();

// 選択画面へ入るために押していたKEY1とKEY2を、完全に離すまで待つ
while (key1IsPressed() || M5.BtnB.isPressed()) {
  M5.update();
  updateKey1();
  delay(10);
}

// 離した直後に残っている押下・解放イベントを消す
M5.update();
updateKey1();

// KEY2を離したときのクリック判定をここで1回消費する
(void)M5.BtnB.wasClicked();

key1PressedEvent = false;
key1ReleasedEvent = false;

key1LongHandled = false;
key2LongHandled = false;
combinationHandled = false;

// 選択画面を確実に表示
drawTargetScreen();

delay(50);
return;
}

// KEY1とKEY2が同時に押されている途中では、通常操作を行わない
if (key1Pressed && key2Pressed) {
  delay(10);
  return;
}

if (targetSelectMode) {

  // 接続先選択画面でKEY1を2秒長押しすると新規登録を開始
  if (!deleteConfirmMode &&
      key1StableState &&
      !key1LongHandled &&
      millis() - key1PressedStartTime >= 2000) {

    key1LongHandled = true;

    targetSelectMode = false;
    pairingMode = true;

    pairConfirmMode = false;
    pairConfirmButtonsReleased = false;
    pairResponsePending = false;

    lastPairRequestTime = 0;

    drawPairingScreen("PAIRING...");

    // 登録開始に使ったKEY1を離すまで待つ
    while (key1IsPressed()) {
      M5.update();
      updateKey1();
      delay(10);
    }

    pairingStartTime = millis();

    key1PressedEvent = false;
key1ReleasedEvent = false;

key1LongHandled = false;
combinationHandled = false;

delay(10);
return;
  }

  // 削除確認中に本体ボタンを押したらキャンセル
 if (deleteConfirmMode && key1PressedEvent) {
  deleteConfirmMode = false;
  deleteTargetIndex = -1;

  key1LongHandled = false;
  key2LongHandled = false;
  combinationHandled = false;

  drawTargetScreen();
  delay(10);
  return;
}

  if (key1PressedEvent && receiverCount > 0) {
    activeReceiver++;
    if (activeReceiver >= receiverCount) activeReceiver = 0;
    receiverReachable = false;
    drawTargetScreen();
  }
    if (M5.BtnB.wasClicked() && !key2LongHandled) {

  if (deleteConfirmMode) {
    return;
  }

  targetSelectMode = false;
  saveReceivers();
  receiverReachable = false;
  drawPairingScreen("CHECKING...");
  sendConnectionCheck();
}

if (M5.BtnB.pressedFor(3000) && !key2LongHandled) {

  key2LongHandled = true;

if (targetSelectMode && receiverCount > 0) {

  if (!deleteConfirmMode) {

    // 1回目の長押し：削除確認
    deleteConfirmMode = true;
    deleteTargetIndex = activeReceiver;
    drawPairingScreen("DELETE?");

  } else {

  // 2回目の長押し：削除実行
deleteReceiver(deleteTargetIndex);

drawPairingScreen("削除完了");

// 削除に使ったKEY2を離すまで待つ
while (M5.BtnB.isPressed()) {
  M5.update();
  updateKey1();
  delay(10);
}

// 削除に使ったKEY2のクリック判定を消す
M5.update();
(void)M5.BtnB.wasClicked();

key1PressedEvent = false;
key1ReleasedEvent = false;

// 新たにKEY1またはKEY2を押すまで表示する
while (true) {
  M5.update();
  updateKey1();

  if (key1PressedEvent || M5.BtnB.wasPressed()) {
    break;
  }

  delay(10);
}

// 「削除完了」を閉じるために押したキーを離すまで待つ
while (key1IsPressed() || M5.BtnB.isPressed()) {
  M5.update();
  updateKey1();
  delay(10);
}

// キーを離したときに残るイベントを消す
M5.update();
updateKey1();
(void)M5.BtnB.wasClicked();

key1PressedEvent = false;
key1ReleasedEvent = false;

key1LongHandled = false;
key2LongHandled = false;
// combinationHandled はここでは戻さない

drawTargetScreen();
  }
}

  delay(300);
}

if (!M5.BtnB.isPressed()) {
  key2LongHandled = false;
}

delay(10);
return;
}
 if (timerSettingMode) {

  // KEY1を押した瞬間：＋1秒
  if (key1PressedEvent) {
    timerSeconds++;

    lastTimerSettingActivity = millis();
    lastTimerRepeatTime = millis();

    sendModeUpdate();
    drawTimerSettingScreen();
  }

  // KEY2を押した瞬間：－1秒
  if (M5.BtnB.wasPressed()) {
    if (timerSeconds > 1) timerSeconds--;
    else timerSeconds = 60;

    lastTimerSettingActivity = millis();
    lastTimerRepeatTime = millis();

    sendModeUpdate();
    drawTimerSettingScreen();
  }

// KEY1長押し：連続で増やす
if (key1StableState &&
    (millis() - key1PressedStartTime >= TIMER_REPEAT_START_MS) &&
    millis() - lastTimerRepeatTime >= TIMER_REPEAT_INTERVAL_MS) {

  lastTimerRepeatTime = millis();
  lastTimerSettingActivity = millis();

  timerSeconds++;

  sendModeUpdate();
  drawTimerSettingScreen();
}

  // KEY2長押し：連続で減らす
  if (M5.BtnB.pressedFor(TIMER_REPEAT_START_MS) &&
      millis() - lastTimerRepeatTime >= TIMER_REPEAT_INTERVAL_MS) {

    lastTimerRepeatTime = millis();
    lastTimerSettingActivity = millis();

    if (timerSeconds > 1) timerSeconds--;
    else timerSeconds = 60;

    sendModeUpdate();
    drawTimerSettingScreen();
  }

  // 何も操作せず3秒たったら設定終了
  if (!key1IsPressed() &&
    !M5.BtnB.isPressed() &&
    millis() - lastTimerSettingActivity >= TIMER_SETTING_TIMEOUT_MS) {

    timerSettingMode = false;
    saveReceivers();
    drawNormalScreen();
    return;
  }

  delay(10);
  return;
}



// KEY2長押しでタイマー設定へ
if (!key2LongHandled &&
    !key1Pressed &&
    M5.BtnB.pressedFor(TIMER_SETTING_HOLD_MS)) {

  key2LongHandled = true;
  timerSettingMode = true;

drawTimerSettingScreen();

while (M5.BtnB.isPressed()) {
  M5.update();
  delay(10);
}

// KEY2を放した時点から3秒を数え始める
lastTimerSettingActivity = millis();
lastTimerRepeatTime = millis();

  return;
}

  if (key1PressedEvent) {
  key1LongHandled = false;

  if (currentMode == MODE_TIMER) {
    timerStartMillis = millis();
    isTimerCounting = true;
    drawNormalScreen();
  }

  sendRelayCommand(ACTION_PRESS);
}

  if (key1ReleasedEvent) {

    
    if (!key1LongHandled && currentMode == MODE_DIRECT) sendRelayCommand(ACTION_RELEASE);
    key1LongHandled = false;
  }
  if (M5.BtnB.wasPressed()) {
  
  key2LongHandled = false;
}
  if (!targetSelectMode && M5.BtnB.wasReleased()) {

  if (!key2LongHandled) {
    currentMode = static_cast<RelayMode>((currentMode + 1) % 3);
    saveReceivers();
    drawNormalScreen();
    sendModeUpdate();
  }

  key2LongHandled = false;
}

// カウントダウンの更新と終了判定
  if (isTimerCounting && currentScreen == SCREEN_NORMAL) {
    long elapsed = (millis() - timerStartMillis) / 1000;
    
    if (elapsed >= timerSeconds) {
  // タイマー終了
  isTimerCounting = false;
  relayOn = false;
  drawNormalScreen();
}
    else {
      // 1秒ごとに画面を部分的に再描画して数値を更新
      static long lastElapsed = -1;
      if (elapsed != lastElapsed) {
        lastElapsed = elapsed;
        drawNormalScreen();
      }
    }
  }

  delay(10);
}
