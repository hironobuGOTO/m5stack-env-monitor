# M5Stack-Env-Monitor

## 依存ライブラリ

- [Queue](https://github.com/SMFSW/Queue) v1.9.0

## 使用 M5Stack ユニット

- TVOC/eCO2 ガスセンサユニット（SGP30）
- 環境センサユニット ver.2（ENV II）

## 使用 M5Stack

- M5STACK-M5GO (3875)

## 概要

M5Stack の LCD ディスプレイに日時、eCO2 濃度、気温、湿度、気圧を表示します。
また、eCO2 濃度を 1 時間ごと、24 時間分を画面下部にグラフで表示します。

その他、eCO2 濃度が高ければ、LED を光らせることでお知らせします。
不快指数が低くなると、状況に応じて LCD ディスプレイの背景色が変化します。
