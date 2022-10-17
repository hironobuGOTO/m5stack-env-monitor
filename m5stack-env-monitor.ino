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
  RGB hot = {255, 165, 0};
  RGB boiling = {255, 127, 80};
};
// 不快指数用警告色の構造体変数の宣言
DiscomfortColor discomfortColor;

// 不快指数の定義 (これの状態が変化したときに描画を変えるため、グローバルで保持)
String discomfortStatus = "comfort";

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

// 最後にCSVに出力した時間を保管しておく変数 (loop() の前後で保持するのでグローバルで保持)
int setCsvWroteTime = 0;

// Sprite クラスのインスタンス化
TFT_eSprite sprite = TFT_eSprite(&M5.Lcd);

// eCO2を棒グラフにするための配列
cppQueue eco2GraphList(sizeof(int), 23, FIFO, true);

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
}

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

// LEDの色と輝度を操作する関数
void setLedColor(RGB rgb, int brightness) {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(rgb.r, rgb.g, rgb.b));
    pixels.setBrightness(brightness);
    pixels.show();
  }
}

// 24bit color を 16bit color に変換する関数
uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
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

// 画面表示する関数
void updateScreen(struct SensorValue latestSensorValue) {
  // Bボタンが押されたとき、色を黒にする
  if (bedroomModeFlg) {
    sprite.fillScreen(TFT_BLACK);
    discomfortStatus = "comfort";
  } else {
    // 不快指数の計算
    float discomfortIndex = ((0.81 * latestSensorValue.temperature) + ((0.01 * latestSensorValue.humidity) * ((0.99 * latestSensorValue.temperature) - 14.3)) + 46.3);
    // 不快指数の画面表示
    if (discomfortIndex < 55 && discomfortStatus != "cold") {
      discomfortStatus = "cold";
      setSpriteBackColor(discomfortColor.cold);
    } else if (discomfortIndex < 60 && discomfortIndex >= 55 && discomfortStatus != "chilly") {
      discomfortStatus = "chilly";
      setSpriteBackColor(discomfortColor.chilly);
    } else if (discomfortIndex < 75 && discomfortIndex >= 60 && discomfortStatus != "comfort") {
      discomfortStatus = "comfort";
      setSpriteBackColor(discomfortColor.comfort);
    } else if (discomfortIndex < 80 && discomfortIndex >= 75 && discomfortStatus != "warm") {
      discomfortStatus = "warm";
      setSpriteBackColor(discomfortColor.warm);
    } else if (discomfortIndex < 85 && discomfortIndex >= 80 && discomfortStatus != "hot") {
      discomfortStatus = "hot";
      setSpriteBackColor(discomfortColor.hot);
    } else if (discomfortIndex >= 85 && discomfortStatus != "boiling") {
      discomfortStatus = "boiling";
      setSpriteBackColor(discomfortColor.boiling);
    }
  }

  // 計測結果をスプライトに入力
  setSpriteMeasurement(sgp.TVOC, sgp.eCO2, latestSensorValue.pressure, latestSensorValue.temperature, latestSensorValue.humidity);

  // スプライトを画面に表示
  sprite.pushSprite(0, 0);

  // eCO2とTVOCの値をシリアルモニタに通信する
  Serial.print("eCO2 "); Serial.print(sgp.eCO2); Serial.print("\t");
  Serial.print("TVOC "); Serial.print(sgp.TVOC); Serial.print("\n");
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

// 構造体を参照して背景色をスプライトに入力する関数
void setSpriteBackColor(RGB rgb) {
  sprite.fillScreen(getColor(rgb.r, rgb.g, rgb.b));
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
        playMp3("/1500ppm.mp3");
        cautionFlg = true;
      }
    } else if (sgp.eCO2 > eco2Threshold.attention) {
      // 注意時のLED点灯と音声鳴動
      setLedColor(eco2ThresholdColor.attention, 25);
      if (!attentionFlg) {
        playMp3("/1000ppm.mp3");     
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

// Queueライブラリを使ったリストにeCO2値を保存
void setEco2GraphList () {
  if (currentDateTime.tm_min != 0) {
    return ;
  } else {
    eco2GraphList.push(&sgp.eCO2);
  }
}

// Queueライブラリを使ったリストを参照し、棒グラフを作る
void drawGraph () {
  for (int i = 23; i > 0; i--){ 
    int eco2Value = 0;
    eco2GraphList.peekIdx(&eco2Value, i); 
    int graphHeightEco2 = map(eco2Value, 400, 5000, 1, 100);
    // (TODO グラフの描写)
  }
}

void loop() {
  // 画面輝度調整の必須処理
  M5.update();
  // 計測
  struct SensorValue latestSensorValue = {0.0, 0.0, 0.0};
  measureSensorValues(latestSensorValue);

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

  // 1分経過ごとにログを保存
  unsigned long lapsedTime = millis();
  Serial.println(lapsedTime);
  if (lapsedTime > setCsvWroteTime + 60000) {
    saveLog(latestSensorValue);
    setCsvWroteTime = lapsedTime;
  }

  
}
