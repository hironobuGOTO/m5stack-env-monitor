# M5Stack-Env-Monitor

## 概要

M5Stack で環境情報を取得し、表示します。

### 機能

- M5Stack の LCD ディスプレイに日時、eCO2 濃度、気温、湿度、気圧を表示します。
- また、eCO2 濃度を 1 時間ごと、24 時間分を画面下部にグラフで表示します。
- eCO2 濃度が高ければ、LED を光らせることでお知らせします。
- 不快指数が低くなると、状況に応じて LCD ディスプレイの背景色が変化します。
- microSD カードに測定値を 1 分ごとに記録します。

## 依存ライブラリ

- [M5Stack](https://github.com/m5stack/M5Stack)　 v0.4.3
- [WiFi](https://www.arduino.cc/reference/en/libraries/wifi/) v1.2.7
- [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) v1.10.5
- [Queue](https://github.com/SMFSW/Queue) v1.9.0

## 使用 M5Stack ユニット

- TVOC/eCO2 ガスセンサユニット（SGP30）
- 環境センサユニット ver.2（ENV II）

## 使用 M5Stack

- M5STACK-M5GO (3875)
