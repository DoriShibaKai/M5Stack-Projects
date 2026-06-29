```markdown
# M5Atom-Projects

## 使用したもの

- AtomLite・・・本体
- M5Stack用回転角ユニット・・・AtomLiteとGrove接続
- TailBAT・・・AtomLiteに挿す電源
- SG90サーボモータ・・・AtomLiteとピン接続
- 3.5mmジャック・・・AtomLiteのピンと接続（Gはブレッドボードで分配）

## 使い方

### Wi-Fi

- Wi-Fiをつなぐ。SSIDは「AtomServo」で始まる名前
- 自分で設定したパスワードを入れる（コード内を書き換えます。デフォルトは `00000000`）
- ブラウザを開き、`http://192.168.4.1/` を開く
- ボタンを押すとサーボが1往復する

### BLE

- アプリ：nRF Connect for MobileでiPhoneで動作確認済み
- 現在は、より簡単な操作方法を検討中

### 物理

- AtomLite本体ボタンで動作
- G33ピンに3.5mmジャックを接続する
  - GND（黒線）はブレッドボードなどで分配してGへ接続する
  - 外部スイッチを接続するとサーボが1往復する
```
