#include <M5Stack.h>
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

Adafruit_BMP280 bme;
SHT3X sht30;

float tmp = 0.0;
float hum = 0.0;
float pressure = 0.0;

// 注意の閾値
const int attentionPoint = 1000;

// 警告の閾値
const int cautionPoint = 1500;

// 不快指数
int discomfortIndex = 0;

// 24bitカラーの構造体
struct fullColor {
  int r;
  int g;
  int b;
};

// 注意のLED色
fullColor attention = {255, 215, 0};

// 警告のLED色
fullColor caution = {255, 69, 0};

// 寒いときの画面背景色
fullColor cold = {0, 0, 205};

// 肌寒いときの画面背景色
fullColor chilly = {135, 206, 235};

// 快適なときの画面背景色
fullColor comfort = {0, 0, 0};

// やや暑いときの画面背景色
fullColor warm = {240, 230, 140};

// 暑いときの画面背景色
fullColor hot = {255,165, 0};

// 暑くてたまらないときの画面背景色
fullColor boiling {255, 127, 80};

// バックライトの輝度を操作する値 0〜5, 初期値は中央値
unsigned char backlightCnt = 2;

// LEDの明滅を決めるフラグ
bool brightLedFlg = true;

//温度、相対湿度から絶対湿度を計算する関数
uint32_t getAbsoluteHumidity(float temperature, float humidity) { 
  // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
  const float absoluteHumidity = 216.7f
                                 * ((humidity / 100.0f) * 6.112f
                                    * exp((17.62f * temperature) / (243.12f + temperature))
                                    / (273.15f + temperature)); // [g/m^3]
  const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f
                                          * absoluteHumidity); // [mg/m^3]
  return absoluteHumidityScaled;
}

// LEDの色と輝度を操作する関数
void setLedColor(int r, int g, int b, int brightness) {
  for (int i = 0; i < NUMPIXELS; i++){
    pixels.setPixelColor(i, pixels.Color(r, g, b));
    pixels.setBrightness(brightness);
    pixels.show();
  }
}

// 24bit color を 16bit color に変換する関数
uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue){
  return ((red>>3)<<11) | ((green>>2)<<5) | (blue>>3);
}

// Sprite クラスのインスタンス化
TFT_eSprite sprite = TFT_eSprite(&M5.Lcd);

// 各数値を計測する関数
void mesureSensorValues() {
  //気圧を測定 (hPa に変換)
  pressure = bme.readPressure() / 100; 
  //sht30 (温湿度センサー) にて、温湿度を測定
  if(sht30.get()==0){
    tmp = sht30.cTemp;
    hum = sht30.humidity;
  }
  //絶対湿度をSGP30にセット
  sgp.setHumidity(getAbsoluteHumidity(tmp, hum));
  //eCO2 TVOC読込に失敗したときのエラー表示
  if (! sgp.IAQmeasure()) { 
    Serial.println("Measurement failed");
    while(1);
  }
  // 不快指数の計算
  discomfortIndex = ((0.81 * tmp) + ((0.01 * hum) * ((0.99 * tmp) - 14.3)) + 46.3);
}

// 画面表示する関数
void updateScreen(){
  // Bボタンが押されたとき、色を黒にする
  if (!brightLedFlg) {
    sprite.fillScreen(TFT_BLACK);
  }else{
    // 不快指数の画面表示
    if (discomfortIndex < 55) {
      setSpriteBackColor(cold);
    }else if (discomfortIndex < 60) {
      setSpriteBackColor(chilly);
    }else if (discomfortIndex < 75) {
      setSpriteBackColor(comfort);
    }else if (discomfortIndex < 80) {
      setSpriteBackColor(warm);
    }else if (discomfortIndex < 85) {
      setSpriteBackColor(hot);
    }else if (discomfortIndex >= 85) {
      setSpriteBackColor(boiling);
    }
  }
  
  // 計測結果をスプライトに入力
  setSpriteMeasurement(sgp.TVOC, sgp.eCO2, pressure, tmp, hum);
  
  // スプライトを画面に表示
  sprite.pushSprite(0, 0);
  
  // eCO2とTVOCの値をシリアルモニタに通信する
  Serial.print("eCO2 "); Serial.print(sgp.eCO2); Serial.print("\t");
  Serial.print("TVOC "); Serial.print(sgp.TVOC); Serial.print("\n");

  //backlightCntに応じて画面輝度を調節する
  M5.Lcd.setBrightness((50 * backlightCnt) + 5);
}

// バックライトの明度を操作する関数
void adjustBacklight(int i) {
  backlightCnt += i;
}

// 計測結果をスプライトに入力する関数
void setSpriteMeasurement(int tvoc, int eco2, float pressure, float tmp, float hum){
  sprite.setCursor(0, 40);
  sprite.printf("TVOC: %4d ppb ", tvoc);
  sprite.setCursor(0, 80);
  sprite.printf("eCO2: %4d ppm ", eco2);
  sprite.setCursor(0, 160);
  sprite.printf("Pres.: %4.1f hPa", pressure);
  sprite.setCursor(0, 200);
  sprite.printf("Temp: %2.1f 'C %2.1f %c ", tmp, hum, '%');
}

// 構造体を参照して背景色をスプライトに入力する関数
void setSpriteBackColor(struct fullColor structure){
  sprite.fillScreen(getColor(structure.r, structure.g, structure.b));
}

// LEDを点灯する関数
void updateLedBar(){
  // LEDの点灯処理
  if (!brightLedFlg) {
    // フラグが立ってないときのLED消灯処理
    setLedColor(0, 0, 0, 0);
  }else{
    // 警告
    if (sgp.eCO2 > cautionPoint) {
      setLedColor(caution.r, caution.g, caution.b, 50);
    // 注意
    }else if(sgp.eCO2 > attentionPoint) {
      setLedColor(attention.r, attention.g, attention.b, 25);
    // 閾値以下のときのLED消灯処理
    }else{
      setLedColor(0, 0, 0, 0);
    }
  }
}

void setup() {
  // LCD, SD, UART, I2C をそれぞれ初期化するかを指定して初期化する
  // (今回SDカードを使っていないため初期化していない)
  M5.begin(true, false, true, true);

  //シリアル通信初期化
  Serial.begin(9600);
  
  // SGP30 が初期化できなかったとき、エラーを返す
  if (! sgp.begin()){
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
  //M5Stack の画面初期化
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setCursor(20, 40);
  // SGP30が動作するまで15秒起動中の表示を出す
  for(int i = 0; i < 15; i++){
    M5.Lcd.printf(".");
    delay(1000);
  }
  // BMP280 が初期化できなかったとき、エラーを返す
  while (!bme.begin(0x76)) {
    //M5.Lcd.println("Could not find a valid BMP280 sensor, check wiring!");
  }

  // スプライトの作成
  sprite.setColorDepth(8);
  sprite.setTextFont(4);
  sprite.setTextSize(1);
  sprite.createSprite(M5.Lcd.width(), M5.Lcd.height());
}
void loop() {
  // 画面輝度調整の必須処理
  M5.update();

  // 計測
  mesureSensorValues();

  // 画面表示
  updateScreen();
  
  // ボタンで画面輝度調節
  if (M5.BtnA.wasPressed()) {
    adjustBacklight(1);
  }
  if (M5.BtnC.wasPressed()) {
    adjustBacklight(-1);
  }
  
  // LED点灯フラグのトグル
  if (M5.BtnB.wasPressed()) {
    brightLedFlg = !brightLedFlg;
  }

  // LEDバーでの表示
  updateLedBar();
}
