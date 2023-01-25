#include <M5Stack.h>
#include <map>

// 計測機用のライブラリ
#include "M5_ENV.h"
#include <Adafruit_SGP30.h>
#include <Adafruit_BMP280.h>
#include "Adafruit_Sensor.h"

// eCO2, TVOCセンサーのインスタンス化
Adafruit_SGP30 sgp;
// 大気圧センサーのインスタンス化
Adafruit_BMP280 bmp280;
// 温湿度センサーのインスタンス化
SHT3X sht30;

uint16_t eco2_base = 37335; //eCO2 baseline仮設定値
uint16_t tvoc_base = 40910; //TVOC baseline仮設定値

// 設定値をJSONに書き出す際に必要なライブラリ
#include <ArduinoJson.h>

#include "config_store.h"
#include "notifier.h"
#include "clock_dial.h"
#include "logger.h"
#include "sprite_manager.h"

// SGP30で計測したeCO2濃度の閾値の構造体
struct Eco2Threshold {
  const int ATTENTION = 1000;
  const int CAUTION = 1500;
};
//eCO2濃度の構造体変数の宣言
Eco2Threshold eco2Threshold;

// 最後に処理文を動かした時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int loopExcuteTime = 0;

// 最後にキューに値を保存した時間を保管しておく変数(loop() の前後で保持するのでグローバルで保持)
int queueWroteTime = 0;

// 最後にCSVに出力した時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int csvWroteTime = 0;

// 画面の更新間隔
const int DISPLAY_REFLESH_TIME = 1000;

// グラフの更新間隔
const int GRAPH_REFLESH_TIME = 3600000;

// センサー計測値の保存間隔
const int SAVE_MEASURE_VALUE_TIME = 60000;

// Notifier クラスのインスタンス化
Notifier notifier;

// Config_store クラスのインスタンス化
ConfigStore configStore;

ClockDial clockDial;

// Logger クラスのインスタンス化
Logger logger(clockDial);

// SpriteManager クラスのインスタンス化
SpriteManager spriteManager(configStore, clockDial);

void initM5stack() {
  // LCD, SD, UART, I2C をそれぞれ初期化するかを指定して初期化する
  M5.begin(true, true, true, true);
  M5.Power.begin();
  // シリアル通信初期化
  Serial.begin(9600);
  Serial.println("M5.began");

  //M5Stack の画面初期化
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setCursor(20, 40);

  // SDカードが初期化できなかったとき、エラーを返す
  if (!SD.begin()) {
    M5.Lcd.println("Card failed, or not present");
    while (1);
  }
}

void initSgp30() {
  // SGP30 が初期化できなかったとき、エラーを返す
  if (!sgp.begin()) {
    Serial.println("Sensor not found :(");
    while (1);
  }
  sgp.softReset();

  // SGP30センサーのIAQアルゴリズムの初期化 softReset後必須
  sgp.IAQinit();

  // IAQ計算のためのキャリブレーションした基準値を要求し、結果をパラメータメモリに格納する。
  // 基準値を設定しない場合はコメントアウト
  sgp.setIAQBaseline(eco2_base, tvoc_base);

  // SGP30が動作するまで15秒起動中の表示を出す
  M5.Lcd.printf("\n  SGP30 waking");
  for (int i = 0; i < 15; i++) {
    M5.Lcd.printf(".");
    delay(1000);
  }
}

void setup() {
  initM5stack();

  initSgp30();

  // BMP280 が初期化できなかったとき、エラーを返す
  while (!bmp280.begin(0x76)) {
    M5.Lcd.println("Could not find a valid BMP280 sensor, check wiring!");
  }

  clockDial.correctTime();

  // SDカードに保存されている初期値の呼び出し
  configStore.load();

  spriteManager.initializeSprite();
}

// measureSensorValues () に必要な関数
//温度、相対湿度から絶対湿度を計算する関数
uint32_t getAbsoluteHumidity(float temperature, float humidity) {
  // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
  const float ABSOLUTE_HUMIDITY = 216.7f
                                  * ((humidity / 100.0f) * 6.112f
                                     * exp((17.62f * temperature) / (243.12f + temperature))
                                     / (273.15f + temperature)); // [g/m^3]

  const uint32_t ABSOLUTE_HUMIDITY_SCALED = static_cast<uint32_t>(1000.0f
      * ABSOLUTE_HUMIDITY); // [mg/m^3]

  return ABSOLUTE_HUMIDITY_SCALED;
}

// 各数値を計測する関数
void measureSensorValues(struct SensorValue & latestSensorValue) {
  //気圧を測定 (hPa に変換)
  latestSensorValue.pressure = bmp280.readPressure() / 100;

  //sht30 (温湿度センサー) にて、温湿度を測定
  if (sht30.get() == 0) {
    latestSensorValue.temperature = sht30.cTemp;
    latestSensorValue.humidity = sht30.humidity;
  }
  //絶対湿度をSGP30にセット
  sgp.setHumidity(getAbsoluteHumidity(latestSensorValue.temperature, latestSensorValue.humidity));

  //eCO2 TVOC読込に失敗したときのエラー表示
  if (! sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    while (1);
  }
}

// LEDを点灯する関数
void updateLedBar() {
  // LEDの点灯処理
  // 寝室モードのときのLED消灯処理
  if (configStore.getBedroomMode()) {
    notifier.notify(notifier.NOTIFY_STATUS::NORMAL);
    return;
  }
  if (sgp.eCO2 > eco2Threshold.CAUTION) {
    notifier.notify(notifier.NOTIFY_STATUS::CAUTION);
  } else if (sgp.eCO2 > eco2Threshold.ATTENTION) {
    notifier.notify(notifier.NOTIFY_STATUS::ATTENTION);
  } else {
    notifier.notify(notifier.NOTIFY_STATUS::NORMAL);
  }
}

void loop() {
  // 経過時間を表す elapsedTime変数の宣言と初期化
  unsigned long elapsedTime = millis();

  // 更新間隔秒秒経過していないときに実行しない早期リターン
  if (elapsedTime < loopExcuteTime + DISPLAY_REFLESH_TIME) {
    return;
  }
  loopExcuteTime = elapsedTime;

  // 画面輝度調整の必須処理
  M5.update();

  // 最新の温湿度、大気圧の計測
  struct SensorValue latestSensorValue = {0.0, 0.0, 0.0};
  measureSensorValues(latestSensorValue);

  // 定数間隔(GRAPH_REFLESH_TIME)ごとにeCO2の値をグラフ用キューに保存
  if (elapsedTime > queueWroteTime + GRAPH_REFLESH_TIME) {
    eco2GraphValueList.push(&sgp.eCO2);
    queueWroteTime = elapsedTime;
  }
  // 定数間隔(SAVE_MEASURE_VALUE_TIME)ごとに計測結果ログをSDカード内CSVに保存
  if (elapsedTime > csvWroteTime + SAVE_MEASURE_VALUE_TIME) {
    logger.saveSensorValue(latestSensorValue);
    csvWroteTime = elapsedTime;
  }
  // 画面表示
  spriteManager.updateScreen(latestSensorValue);

  // ボタンAとCで画面輝度調節
  if (M5.BtnA.wasPressed()) {
    configStore.adjustBacklightLevel(1);
  }
  if (M5.BtnC.wasPressed()) {
    configStore.adjustBacklightLevel(-1);
  }
  // 寝室モードフラグのトグル
  if (M5.BtnB.wasPressed()) {
    configStore.toggleBedroomMode();
  }
  // LEDバーでの表示
  updateLedBar();
}
