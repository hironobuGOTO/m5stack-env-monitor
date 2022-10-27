#include <Adafruit_NeoPixel.h>
#define PIN 15
#define NUMPIXELS 10

// ESP8266Audioライブラリ
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

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

class Notifier {

  public:
    enum NOTIFY_STATUS {
      NORMAL,
      CAUTION,
      ATTENTION
    };

    Notifier() {
      // M5Stack側面LEDの起動
      pixels.begin();
    }

    void notify(NOTIFY_STATUS status) {
      if (status == NORMAL) {
        // 閾値以下のときのLED消灯処理
        setLedColor(eco2ThresholdColor.normal, 0);
        cautionFlg = false;
        attentionFlg = false;
      } else if (status == CAUTION) {
        // 警告時のLED点灯と音声鳴動
        setLedColor(eco2ThresholdColor.caution, 50);
        if (!cautionFlg) {
          //playMp3("/1500ppm.mp3");
          cautionFlg = true;
        }
      } else if (status == ATTENTION) {
        // 注意時のLED点灯と音声鳴動
        setLedColor(eco2ThresholdColor.attention, 25);
        if (!attentionFlg) {
          //playMp3("/1000ppm.mp3");
          attentionFlg = true;
          cautionFlg = false;
        }
      }
    }

  private:
    boolean attentionFlg = false;
    boolean cautionFlg = false;

    Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

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
      AudioGeneratorMP3 *mp3 = new AudioGeneratorMP3();
      AudioFileSourceSD *file = new AudioFileSourceSD(filename);;
      AudioOutputI2S *out = new AudioOutputI2S(0, 1);

      out->SetOutputModeMono(true);
      out->SetGain(1.0);
      mp3->begin(file, out);

      // 音声ファイルが終了するまで他の処理を中断する
      while (mp3->isRunning()) {
        if (!mp3->loop()) mp3->stop();
      }
    }

};
