#include <M5Stack.h>
#include <WiFi.h>
#include <map>

// 計測機用のライブラリ
#include "M5_ENV.h"
#include <Adafruit_SGP30.h>
#include <Adafruit_BMP280.h>
#include "Adafruit_Sensor.h"

Adafruit_SGP30 sgp;
Adafruit_BMP280 bmp280;
SHT3X sht30;

uint16_t eco2_base = 37335; //eCO2 baseline仮設定値
uint16_t tvoc_base = 40910; //TVOC baseline仮設定値

// 設定値をJSONに書き出す際に必要なライブラリ
#include <ArduinoJson.h>

// 棒グラフを生成する配列をキューで処理するためのライブラリ
#include <cppQueue.h>

#include "config.h"
#include "notifier.h"
#include "config_store.h"
#include "logger.h"

// SGP30で計測したeCO2濃度の閾値の構造体
struct Eco2Threshold {
  int attention = 1000;
  int caution = 1500;
};
//eCO2濃度の構造体変数の宣言
Eco2Threshold eco2Threshold;

// 不快指数が快適じゃないときの警告色の構造体を定義
struct DiscomfortColor {
  RGB cold = {0, 0, 205};
  RGB chilly = {135, 206, 235};
  RGB comfort = {0, 0, 0};
  RGB warm = {240, 230, 140};
  RGB hot = {255, 140, 0};
  RGB boiling = {255, 127, 80};
};
// 不快指数用警告色の構造体変数の宣言
DiscomfortColor discomfortColor;

// 不快指数の定義 (これの状態が変化したときに描画を変えるため、グローバルで保持)
RGB discomfortStatusColor = discomfortColor.comfort;

// 最後に処理文を動かした時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int loopExcuteTime = 0;

// 最後にキューに値を保存した時間を保管しておく変数(loop() の前後で保持するのでグローバルで保持)
int queueWroteTime = 0;

// 最後にCSVに出力した時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int csvWroteTime = 0;

// Notifier クラスのインスタンス化
Notifier notifier;

// Config_store クラスのインスタンス化
ConfigStore configStore;

// Logger クラスのインスタンス化
Logger logger;

// Sprite クラスのインスタンス化
TFT_eSprite sprite = TFT_eSprite(&M5.Lcd);

// eCO2を棒グラフにするためのキュー配列
cppQueue eco2GraphValueList(sizeof(int), 23, FIFO, true);


// setup() 関数に必要な関数
// Queueライブラリを使ったリストを初期化する関数
void initializeEco2GraphValueList() {
  const int MAPPED_VALUE = 400;
  for (int i = 0; i <= 23; i++) {
    eco2GraphValueList.push(&MAPPED_VALUE);
  }
}

void setup() {
  // LCD, SD, UART, I2C をそれぞれ初期化するかを指定して初期化する
  M5.begin(true, true, true, true);
  M5.Power.begin();

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

  // Wi-Fiを起動してntpから現在時刻を取得する
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Wi-Fi接続が完了するのを待つ
  M5.Lcd.printf("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    M5.Lcd.printf(".");
    delay(1000);
  }
  // 時刻の補正
  configTime(9 * 3600L, 0, "ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp");

  // シリアル通信初期化
  Serial.begin(9600);

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
  // BMP280 が初期化できなかったとき、エラーを返す
  while (!bmp280.begin(0x76)) {
    M5.Lcd.println("Could not find a valid BMP280 sensor, check wiring!");
  }
  // スプライトの作成
  sprite.setColorDepth(8);
  sprite.setTextFont(4);
  sprite.setTextSize(1);
  sprite.createSprite(M5.Lcd.width(), M5.Lcd.height());

  // 初期値の呼び出し
  configStore.load();

  // グラフ表示用のキューを初期化する関数を呼び出し
  initializeEco2GraphValueList();
}

// loop () 関数に必要な関数
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
void measureSensorValues(struct SensorValue& latestSensorValue) {
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

// Queueライブラリを使ったリストに呼び出されるごとにeCO2値を保存する関数
void setEco2GraphValueList() {
  eco2GraphValueList.push(&sgp.eCO2);
}

// setSpriteBackColor () に必要な関数
// 24bit color を 16bit color に変換する関数
uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
}

// 構造体を参照して背景色をスプライトに入力する関数
void setSpriteBackColor(RGB rgb) {
  sprite.fillScreen(getColor(rgb.r, rgb.g, rgb.b));
}

// RGBが同じことを確かめる関数
bool compareRGBEqual(struct RGB a, struct RGB b) {
  return a.r == b.r &&
         a.g == b.g &&
         a.b == b.b;
}

// 計測結果をスプライトに入力する関数
void setSpriteMeasurement(int tvoc, int eco2, float pressure, float temperature, float humidity) {
  sprite.setCursor(0, 10);
  sprite.printf("Pres.: %4.1f hPa", pressure);
  sprite.setCursor(0, 40);
  sprite.printf("Temp: %2.1f 'C %2.1f %c ", temperature, humidity, '%');
  sprite.setCursor(0, 70);
  sprite.printf("TVOC: %4d ppb ", tvoc);
  sprite.setCursor(0, 100);
  sprite.printf("eCO2: %4d ppm ", eco2);
}

// updateScreen () に必要な関数
// eCO2の値が前回計測したときから下回っていないことを確かめる関数
bool compareEco2Value(int comparisonValue[]) {
  for (int i = 0; i < 23; i++) {
    if (comparisonValue[i] < comparisonValue[(i + 1)]) {
      return 1;
    }
  }
  return 0;
}

// 画面表示する関数
void updateScreen(struct SensorValue latestSensorValue) {
  // Bボタンが押されたとき、色を黒にする
  if (configStore.getBedroomMode()) {
    discomfortStatusColor = discomfortColor.comfort;
    setSpriteBackColor(discomfortStatusColor);
  } else {
    // 不快指数の計算
    const float DISCOMFORT_INDEX = ((0.81 * latestSensorValue.temperature) + ((0.01 * latestSensorValue.humidity) * ((0.99 * latestSensorValue.temperature) - 14.3)) + 46.3);
    // 不快指数の画面表示
    if (DISCOMFORT_INDEX < 55 && !compareRGBEqual(discomfortStatusColor, discomfortColor.cold)) {
      discomfortStatusColor = discomfortColor.cold;
    }
    if (DISCOMFORT_INDEX < 60 && DISCOMFORT_INDEX >= 55 && !compareRGBEqual(discomfortStatusColor, discomfortColor.chilly)) {
      discomfortStatusColor = discomfortColor.chilly;
    }
    if (DISCOMFORT_INDEX < 75 && DISCOMFORT_INDEX >= 60 && !compareRGBEqual(discomfortStatusColor, discomfortColor.comfort)) {
      discomfortStatusColor = discomfortColor.comfort;
    }
    if (DISCOMFORT_INDEX < 80 && DISCOMFORT_INDEX >= 75 && !compareRGBEqual(discomfortStatusColor, discomfortColor.warm)) {
      discomfortStatusColor = discomfortColor.warm;
    }
    if (DISCOMFORT_INDEX < 85 && DISCOMFORT_INDEX >= 80 && !compareRGBEqual(discomfortStatusColor, discomfortColor.hot)) {
      discomfortStatusColor = discomfortColor.hot;
    }
    if (DISCOMFORT_INDEX >= 85 && !compareRGBEqual(discomfortStatusColor, discomfortColor.boiling)) {
      discomfortStatusColor = discomfortColor.boiling;
    }
    setSpriteBackColor(discomfortStatusColor);
  }
  // キューの値を配列に挿入
  int comparisonEco2Value[23];
  for (int i = 0; i <= 23; i++) {
    eco2GraphValueList.peekIdx(&comparisonEco2Value[i], i);
  }
  // グラフが下がっているとき、下がることを反映するためにグラフ領域を背景色で塗りつぶす
  if (compareEco2Value(comparisonEco2Value)) {
    setSpriteBackColor(discomfortStatusColor);
  }
  // 計測結果をスプライトに入力
  setSpriteMeasurement(sgp.TVOC, sgp.eCO2, latestSensorValue.pressure, latestSensorValue.temperature, latestSensorValue.humidity);

  // 過去のキューに入れたeCO2値をグラフに描写
  for (int i = 0; i <= 23; i++) {
    int eco2Value = 0;
    int graphHeightEco2 = 0;
    bool peekIndex = eco2GraphValueList.peekIdx(&eco2Value, i);

    //eCO2が1500を超えたときグラフ最大値まで持っていく
    if (eco2Value > 1500) {
      graphHeightEco2 = 100;
    } else {
      graphHeightEco2 = map(eco2Value, 0, 1500, 1, 100);
    }
    if (graphHeightEco2 < 67) {
      sprite.fillRect((i * 13), (240 - graphHeightEco2), 13, graphHeightEco2, TFT_GREEN);
    } else if (graphHeightEco2 < 99) {
      sprite.fillRect((i * 13), (240 - graphHeightEco2), 13, graphHeightEco2, TFT_YELLOW);
    } else {
      sprite.fillRect((i * 13), (240 - graphHeightEco2), 13, graphHeightEco2, TFT_RED);
    }
  }
  // 現在のeCO2値をグラフに描写
  const int LATEST_GRAPH_HEIGHT_ECO2 = map((int)sgp.eCO2, 0, 1500, 1, 100);
  if (LATEST_GRAPH_HEIGHT_ECO2 < 67) {
    sprite.fillRect(299, (240 - LATEST_GRAPH_HEIGHT_ECO2), 21, LATEST_GRAPH_HEIGHT_ECO2, TFT_GREEN);
  } else if (LATEST_GRAPH_HEIGHT_ECO2 < 99) {
    sprite.fillRect(299, (240 - LATEST_GRAPH_HEIGHT_ECO2), 21, LATEST_GRAPH_HEIGHT_ECO2, TFT_YELLOW);
  } else {
    sprite.fillRect(299, (240 - LATEST_GRAPH_HEIGHT_ECO2), 21, LATEST_GRAPH_HEIGHT_ECO2, TFT_RED);
  }
  // スプライトを画面に表示
  sprite.pushSprite(0, 0);

  // 寝室モードのとき輝度を落とす
  if (configStore.getBedroomMode()) {
    M5.Lcd.setBrightness(5);
  } else {
    //backlightCntに応じて画面輝度を調節する
    M5.Lcd.setBrightness((50 * configStore.getBacklightLevel()) + 5);
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
  if (sgp.eCO2 > eco2Threshold.caution) {
    notifier.notify(notifier.NOTIFY_STATUS::CAUTION);
  } else if (sgp.eCO2 > eco2Threshold.attention) {
    notifier.notify(notifier.NOTIFY_STATUS::ATTENTION);
  } else {
    notifier.notify(notifier.NOTIFY_STATUS::NORMAL);
  }
}

void loop() {
  // 経過時間を表す elapsedTime変数の宣言と初期化
  unsigned long elapsedTime = millis();

  // 1秒経過していないときに実行しない早期リターン
  if (elapsedTime < loopExcuteTime + 1000) {
    return;
  }
  loopExcuteTime = elapsedTime;

  // 画面輝度調整の必須処理
  M5.update();

  // 計測
  struct SensorValue latestSensorValue = {0.0, 0.0, 0.0};
  measureSensorValues(latestSensorValue);

  // 1時間経過ごとにeCO2の値をリストに保存
  if (elapsedTime > queueWroteTime + 3600000) {
    setEco2GraphValueList();
    queueWroteTime = elapsedTime;
  }
  // 1分経過ごとにログを保存
  if (elapsedTime > csvWroteTime + 60000) {
    logger.saveLog(latestSensorValue);
    csvWroteTime = elapsedTime;
  }
  // 画面表示
  updateScreen(latestSensorValue);

  // ボタンで画面輝度調節
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
