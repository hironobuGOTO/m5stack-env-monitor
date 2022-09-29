#include "config.h"
#include <M5Stack.h>
#include <WiFi.h>
#include <Adafruit_SGP30.h>
Adafruit_SGP30 sgp;
uint16_t eco2_base = 37335; //eCO2 baseline仮設定値
uint16_t tvoc_base = 40910; //TVOC baseline仮設定値

#include <Adafruit_NeoPixel.h>
#define PIN 15
#define NUMPIXELS 10
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

#include "M5_ENV.h"
#include <Adafruit_BMP280.h>
#include "Adafruit_Sensor.h"

Adafruit_BMP280 bmp280;
SHT3X sht30;

// ESP8266Audioライブラリ
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

#include <ArduinoJson.h>

// sht30 で計測した値の構造体
struct measureValue {
  String name;
  float value;
};

struct measureValue latestSensorValue[3] = {
  {"temperature", 0.0},
  {"humidity", 0.0},
  {"pressure", 0.0}
};

struct eco2threshold {
  const int THRESHOLD;
};

// 注意の閾値
struct eco2threshold attention;
attention.threshold = 1000;

// 警告の閾値
struct eco2threshold caution;
caution.threshold = 1500;

// 不快指数
int discomfortIndex = 0;

// 24bitカラーの構造体
struct RGB {
  int r;
  int g;
  int b;
};

struct StatusColor {
  String name;
  struct RGB color;
};

struct StatusColor Eco2Color[2] = {
  {"colorAttention", 255, 215, 0},
  {"colorCaution", 255, 69, 0}
};

struct StatusColor DiscomfortColor[6] = {
  {"colorCold", 0, 0, 205},
  {"colorChilly", 135, 206, 235},
  {"colorComfort", 0, 0, 0},
  {"colorWarm", 240, 230, 140},
  {"colorHot", 255, 165, 0},
  {"colorBoiling", 255, 127, 80}
};


// 不快指数状況の初期値
String discomfortStatus = "comfort";

// バックライトの輝度を操作する値 0〜5, 初期値は中央値
unsigned char backlightCnt = 2;

// 寝室モードかを確認するフラグ
bool bedroomModeFlg = false;

// 二酸化炭素濃度が高まった後下がるまで喋らないようにするフラグ
bool cautionFlg = false;
bool attentionFlg = false;

// 変更があったときのみSDカードに保存するための一時退避所
unsigned char tmpBacklightCnt;

// 寝室モードかを判別するフラグ
bool tmpBedroomModeFlg;

// 最後にCSVに出力した時間を保管しておく変数
int setCsvWroteTime = 0;

// 時間をtm型で取得する変数
struct tm currentDateTime;

// Sprite クラスのインスタンス化
TFT_eSprite sprite = TFT_eSprite(&M5.Lcd);

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
  // 設定ファイルの読み込み
  for (int i = 0; i < configFile.size(); i++) {
    configArray[i] = configFile.read();
  }
  configFile.close();
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
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.printf("SGP30 waking...");
  // SGP30が動作するまで15秒起動中の表示を出す
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
void setLedColor(int r, int g, int b, int brightness) {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
    pixels.setBrightness(brightness);
    pixels.show();
  }
}

// 24bit color を 16bit color に変換する関数
uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
}

// 各数値を計測する関数
void measureSensorValues() {
  //気圧を測定 (hPa に変換)
  pressure = bmp280.readPressure() / 100;
  //sht30 (温湿度センサー) にて、温湿度を測定
  if (sht30.get() == 0) {
    latestSensorValue[0].value = sht30.cTemp;
    latestSensorValue[1].value = sht30.humidity;
  }
  //絶対湿度をSGP30にセット
  sgp.setHumidity(getAbsoluteHumidity(latestSensorValue[0].value, latestSensorValue[1].value));
  //eCO2 TVOC読込に失敗したときのエラー表示
  if (! sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    while (1);
  }
  // 不快指数の計算
  discomfortIndex = ((0.81 * latestSensorValue[0].value) + ((0.01 * latestSensorValue[1].value) * ((0.99 * latestSensorValue[0].value) - 14.3)) + 46.3);
}

// 画面表示する関数
void updateScreen() {
  // Bボタンが押されたとき、色を黒にする
  if (bedroomModeFlg) {
    sprite.fillScreen(TFT_BLACK);
    discomfortStatus = "comfort";
  } else {
    // 不快指数の画面表示
    if (discomfortIndex < 55 && discomfortStatus != "cold") {
      discomfortStatus = "cold";
      setSpriteBackColor(DiscomfortColor[0]);
    } else if (discomfortIndex < 60 && discomfortIndex >= 55 && discomfortStatus != "chilly") {
      discomfortStatus = "chilly";
      setSpriteBackColor(DiscomfortColor[1]);
    } else if (discomfortIndex < 75 && discomfortIndex >= 60 && discomfortStatus != "comfort") {
      discomfortStatus = "comfort";
      setSpriteBackColor(DiscomfortColor[2]);
    } else if (discomfortIndex < 80 && discomfortIndex >= 75 && discomfortStatus != "warm") {
      discomfortStatus = "warm";
      setSpriteBackColor(DiscomfortColor[3]);
    } else if (discomfortIndex < 85 && discomfortIndex >= 80 && discomfortStatus != "hot") {
      discomfortStatus = "hot";
      setSpriteBackColor(DiscomfortColor[4]);
    } else if (discomfortIndex >= 85 && discomfortStatus != "boiling") {
      discomfortStatus = "boiling";
      setSpriteBackColor(DiscomfortColor[5]);
    }
  }

  // 計測結果をスプライトに入力
  setSpriteMeasurement(sgp.TVOC, sgp.eCO2, latestSensorValue[2].value, latestSensorValue[0].value, latestSensorValue[1].value);

  // スプライトを画面に表示
  sprite.pushSprite(0, 0);

  // eCO2とTVOCの値をシリアルモニタに通信する
  Serial.print("eCO2 "); Serial.print(sgp.eCO2); Serial.print("\t");
  Serial.print("TVOC "); Serial.print(sgp.TVOC); Serial.print("\n");

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
  sprite.setCursor(0, 40);
  sprite.printf("TVOC: %4d ppb ", tvoc);
  sprite.setTextSize(1);
  sprite.setCursor(0, 80);
  sprite.printf("eCO2: %4d ppm ", eco2);
  sprite.setTextSize(1);
  sprite.setCursor(0, 160);
  sprite.printf("Pres.: %4.1f hPa", pressure);
  sprite.setCursor(0, 200);
  sprite.printf("Temp: %2.1f 'C %2.1f %c ", temperature, humidity, '%');
}

// 構造体を参照して背景色をスプライトに入力する関数
void setSpriteBackColor(struct StatusColor statusColor) {
  sprite.fillScreen(getColor(statusColor.color.r, statusColor.color.g, statusColor.color.b));
}

// LEDを点灯する関数
void updateLedBar() {
  // LEDの点灯処理
  if (bedroomModeFlg) {
    // フラグが立っているときのLED消灯処理
    setLedColor(0, 0, 0, 0);
  } else {
    if (sgp.eCO2 > caution.threshold) {
      // 警告
      setLedColor(Eco2Color[1].color.r, Eco2Color[1].color.g, Eco2Color[1].color.b, 50);
      if (!cautionFlg) {
        playMp3("/1500ppm.mp3");
        cautionFlg = true;
      }
    } else if (sgp.eCO2 > attention.threshold) {
      // 注意
      setLedColor(Eco2Color[0].color.r, Eco2Color[0].color.g, Eco2Color[0].color.b, 25);
      if (!attentionFlg) {
        playMp3("/1000ppm.mp3");
        attentionFlg = true;
        cautionFlg = false;
      }
    } else {
      // 閾値以下のときのLED消灯処理
      setLedColor(0, 0, 0, 0);
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
  if (tmpBacklightCnt != backlightCnt || tmpBedroomModeFlg != bedroomModeFlg) {
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
void saveLog() {
  bool getTime = getLocalTime(&currentDateTime);
  String measureDay = getDateString(currentDateTime);
  String measureTime = getTimeString(currentDateTime);
  File measureValues = SD.open("/measure_values.csv", FILE_APPEND);
  measureValues.print(measureDay + "," + measureTime + "," + sgp.eCO2 + "," + sgp.TVOC + "," + latestSensorValue[0].value + "," + latestSensorValue[1].value + "," + latestSensorValue[2].value + "\n");
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

void loop() {
  // 画面輝度調整の必須処理
  M5.update();

  // 計測
  measureSensorValues();

  // 画面表示
  updateScreen();

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
    saveLog();
    setCsvWroteTime = lapsedTime;
  }
}
