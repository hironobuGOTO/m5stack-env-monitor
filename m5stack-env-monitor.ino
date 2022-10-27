#include "config.h"
#include <M5Stack.h>
#include <WiFi.h>
#include <map>

#include <Adafruit_NeoPixel.h>
#define PIN 15
#define NUMPIXELS 10
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

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

// ESP8266Audioライブラリ
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// 設定値をJSONに書き出す際に必要なライブラリ
#include <ArduinoJson.h>

// 棒グラフを生成する配列をキューで処理するためのライブラリ
#include <cppQueue.h>

// SHT30で計測した値の構造体
struct SensorValue {
  float temperature;
  float humidity;
  float pressure;
};

// SGP30で計測したeCO2濃度の閾値の構造体
struct Eco2Threshold {
  int attention = 1000;
  int caution = 1500;
};
//eCO2濃度の構造体変数の宣言
Eco2Threshold eco2Threshold;

// 24bitカラーの構造体
struct RGB {
  int r;
  int g;
  int b;
};

// eCO2計測値の閾値を超えたときの警告色の構造体を定義
struct Eco2ThresholdColor {
  RGB attention = {255, 215, 0};
  RGB caution = {255, 69, 0};
  RGB normal = {0, 0, 0};
};
// eCO2用警告色の構造体変数の宣言
Eco2ThresholdColor eco2ThresholdColor;

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

// バックライトの輝度を操作する値 0〜5 (初期値は中央値, loop() の前後で保持するのでグローバルで保持)
unsigned char backlightCnt = 2;
// 変更があったときのみSDカードに保存するための一時退避所 (loop() の前後で保持するのでグローバルで保持)
unsigned char tmpBacklightCnt;

// 時間をtm型で取得する変数
struct tm currentDateTime;

// 寝室モードかを確認するフラグ(loop() の前後で保持するのでグローバルで保持)
bool bedroomModeFlg = false;

// 二酸化炭素濃度が高まった後下がるまで喋らないようにするフラグ(loop() の前後で保持するのでグローバルで保持)
bool cautionFlg = false;
bool attentionFlg = false;

// 寝室モードかを判別するフラグ(loop() の前後で保持するのでグローバルで保持)
bool tmpBedroomModeFlg;

// 最後に処理文を動かした時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int loopExcuteTime = 0;

// 最後にキューに値を保存した時間を保管しておく変数(loop() の前後で保持するのでグローバルで保持)
int queueWroteTime = 0;

// 最後にCSVに出力した時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int csvWroteTime = 0;

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

  // SDカードに保存された設定情報を読み込み、初期値に反映させる
  File configFile = SD.open("/config.txt", FILE_READ);
  char configArray[configFile.size()];
  // 設定ファイルの読み込み (1文字ずつ)
  for (int i = 0; i < configFile.size(); i++) {
    configArray[i] = configFile.read();
  }
  configFile.close();

  // JSONを読んで配列に変換
  DynamicJsonDocument configJson(1024);
  deserializeJson(configJson, configArray);

  backlightCnt = configJson["backlightCnt"];
  bedroomModeFlg = configJson["bedroomFlag"];

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
  // M5Stack側面LEDの起動
  pixels.begin();
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

// saveLog () に必要な関数
// tm オブジェクトから日付文字列 (例: "12/1") を返す関数
String getDateString(struct tm currentDateTime) {
  int month = currentDateTime.tm_mon + 1;
  String dateString = String(month) + "/" + String(currentDateTime.tm_mday);
  return dateString;
}

// tm オブジェクトから時刻文字列 (例: "00:00") を返す関数
String getTimeString(struct tm currentDateTime) {
  String timeString = String(currentDateTime.tm_hour) + ":" + String(currentDateTime.tm_min);
  return timeString;
}

// 計測した値をSDカードに保存する関数
void saveLog(struct SensorValue latestSensorValue) {
  // ローカル時間を取得する
  bool getTime = getLocalTime(&currentDateTime);
  String measureDay = getDateString(currentDateTime);
  String measureTime = getTimeString(currentDateTime);
  // SDカードにログを追加する
  File measureValues = SD.open("/measure_values.csv", FILE_APPEND);
  measureValues.print(measureDay + "," + measureTime + "," + sgp.eCO2 + "," + sgp.TVOC + "," + latestSensorValue.temperature + "," + latestSensorValue.humidity + "," + latestSensorValue.pressure + "\n");
  measureValues.close();
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

// 画面表示する関数
void updateScreen(struct SensorValue latestSensorValue) {
  // Bボタンが押されたとき、色を黒にする
  if (bedroomModeFlg) {
    discomfortStatusColor = discomfortColor.comfort;
    setSpriteBackColor(discomfortColor.comfort);
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
  if (bedroomModeFlg) {
    M5.Lcd.setBrightness(5);
  } else {
    //backlightCntに応じて画面輝度を調節する
    M5.Lcd.setBrightness((50 * backlightCnt) + 5);
  }
}

// バックライトの明度を操作する関数
void adjustBacklight(int i) {
  backlightCnt += i;
  if (backlightCnt > 5) {
    backlightCnt = 5;
  } else if (backlightCnt < 0) {
    backlightCnt = 0;
  }
}

// updateLedBar () に必要な関数
// LEDの色と輝度を操作する関数
void setLedColor(RGB rgb, int brightness) {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(rgb.r, rgb.g, rgb.b));
    pixels.setBrightness(brightness);
    pixels.show();
  }
}

// MP3ファイルを再生する関数
void playMp3(char *filename) {
  // mp3ファイルの変数定義
  AudioGeneratorMP3 *mp3;
  AudioFileSourceSD *file;
  AudioOutputI2S *out;

  file = new AudioFileSourceSD(filename);
  out = new AudioOutputI2S(0, 1);
  out->SetOutputModeMono(true);
  out->SetGain(1.0);
  mp3 = new AudioGeneratorMP3();
  mp3->begin(file, out);
  // 音声ファイルが終了するまで他の処理を中断する
  while (mp3->isRunning()) {
    if (!mp3->loop()) mp3->stop();
  }
}

// LEDを点灯する関数
void updateLedBar() {
  // LEDの点灯処理
  // 寝室モードのときのLED消灯処理
  if (bedroomModeFlg) {
    setLedColor(eco2ThresholdColor.normal, 0);
    return ;
  } else {
    if (sgp.eCO2 > eco2Threshold.caution) {
      // 警告時のLED点灯と音声鳴動
      setLedColor(eco2ThresholdColor.caution, 50);
      if (!cautionFlg) {
        //playMp3("/1500ppm.mp3");
        cautionFlg = true;
      }
    } else if (sgp.eCO2 > eco2Threshold.attention) {
      // 注意時のLED点灯と音声鳴動
      setLedColor(eco2ThresholdColor.attention, 25);
      if (!attentionFlg) {
        //playMp3("/1000ppm.mp3");
        attentionFlg = true;
        cautionFlg = false;
      }
    }  else {
      // 閾値以下のときのLED消灯処理
      setLedColor(eco2ThresholdColor.normal, 0);
      cautionFlg = false;
      attentionFlg = false;
    }
  }
}

// 生成したJSONをSDカードに出力する関数
void saveConfig() {
  // 設定が変わったときのみ書き込む
  if ((tmpBacklightCnt == backlightCnt) && (tmpBedroomModeFlg == bedroomModeFlg)) {
    return;
  } else {
    // configFile の定義
    File configFile = SD.open("/config.txt", FILE_WRITE);

    // 計測値を保存しておくJSON
    DynamicJsonDocument measurement(1024);

    // JSONに値を入力
    measurement["backlightCnt"] = backlightCnt;
    measurement["bedroomFlag"] = bedroomModeFlg;

    // JSONのシリアライズ
    String output;
    serializeJson(measurement, output);

    // SDカードへの書き込みと処理終了
    configFile.println(output);
    configFile.close();

    // 現在の設定情報を一時保存
    tmpBacklightCnt = backlightCnt;
    tmpBedroomModeFlg = bedroomModeFlg;
  }
}

void loop() {
  // 1秒ごとに本文を実行する
  unsigned long elapsedTime = millis();
  if (elapsedTime < loopExcuteTime + 1000) {
    return ;
  }
  loopExcuteTime = elapsedTime;
  // 画面輝度調整の必須処理
  M5.update();
  // 計測
  struct SensorValue latestSensorValue = {0.0, 0.0, 0.0};
  measureSensorValues(latestSensorValue);

  // 1時間に一回eCO2の値をリストに保存
  if (elapsedTime > queueWroteTime + 3600000) {
    setEco2GraphValueList();
    queueWroteTime = elapsedTime;
  }

  // 1分経過ごとにログを保存
  if (elapsedTime > csvWroteTime + 60000) {
    saveLog(latestSensorValue);
    csvWroteTime = elapsedTime;
  }

  // 画面表示
  updateScreen(latestSensorValue);

  // ボタンで画面輝度調節
  if (M5.BtnA.wasPressed()) {
    adjustBacklight(1);
  }
  if (M5.BtnC.wasPressed()) {
    adjustBacklight(-1);
  }

  // 寝室モードフラグのトグル
  if (M5.BtnB.wasPressed()) {
    bedroomModeFlg = !bedroomModeFlg;
  }

  // LEDバーでの表示
  updateLedBar();

  // 生成した設定情報JSONをSDカードに出力する
  saveConfig();
}
