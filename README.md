# M5Atom-Projects

使用したもの

・AtomLite・・・本体
・M5Stack用回転角ユニット ・・・AtomLiteとGrove接続
・TailBAT・・・AtomLiteに挿す電源
・SG90サーボモータ・・・AtomLiteとピン接続
・3.5mmジャック・・・AtomLiteのピンと接続（Gはブレッドボードで分配）


使い方
【Wi-Fi】
・Wi-Fiをつなぐ。SSIDは「AtomServo」で始まる名前
・自分で設定したパスワードを入れる（コード内を書き換えます。デフォルトは00000000）
・ブラウザを開き、http://192.168.4.1/をアドレスに入れる
・ボタンを押すと一往復する

【BLE】
・アプリ：nRF Connect for Mobile でiPhoneで動作確認済みだが複雑なので別の方法検討中

【物理】
・AtomLite本体ボタンで動作
・G33ピンに3.5ｍｍジャックをつないで（黒コードはブレッドボードなどで分配してGへ）そこにスイッチを挿すと外部スイッチ入力で動作
