// 棒グラフを生成する配列をキューで処理するためのライブラリ
#include <cppQueue.h>

// Sprite クラスのインスタンス化
TFT_eSprite sprite = TFT_eSprite(&M5.Lcd);

// eCO2を棒グラフにするためのキュー配列
cppQueue eco2GraphValueList(sizeof(int), 23, FIFO, true);

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

class SpriteManager {

  public:
    SpriteManager(ConfigStore &configStore): configStore(configStore) {
      // グラフ表示用のキューを初期化する関数を呼び出し
      initializeEco2GraphValueList();
    };
    // スプライトの作成
    void initSprite() {
      sprite.setColorDepth(8);
      sprite.setTextFont(4);
      sprite.setTextSize(1);
      sprite.createSprite(M5.Lcd.width(), M5.Lcd.height());
    }

    // 画面表示する関数
    void updateScreen(struct SensorValue latestSensorValue) {
      // Bボタンが押されたとき、色を黒にする
      if (configStore.getBedroomMode()) {
        discomfortStatusColor = discomfortColor.comfort;
        setBackColor(discomfortStatusColor);
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
        setBackColor(discomfortStatusColor);
      }
      // キューの値を配列に挿入
      int comparisonEco2Value[23];
      for (int i = 0; i <= 23; i++) {
        eco2GraphValueList.peekIdx(&comparisonEco2Value[i], i);
      }
      // グラフが下がっているとき、下がることを反映するためにグラフ領域を背景色で塗りつぶす
      if (compareEco2Value(comparisonEco2Value)) {
        setBackColor(discomfortStatusColor);
      }
      // 計測結果をスプライトに入力
      setMeasurement(sgp.TVOC, sgp.eCO2, latestSensorValue.pressure, latestSensorValue.temperature, latestSensorValue.humidity);

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

  private:

    ConfigStore &configStore;

    // Queueライブラリを使ったリストを初期化する関数
    void initializeEco2GraphValueList() {
      const int MAPPED_VALUE = 400;
      for (int i = 0; i <= 23; i++) {
        eco2GraphValueList.push(&MAPPED_VALUE);
      }
    }

    // 24bit color を 16bit color に変換する関数
    uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue) {
      return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    }

    // 構造体を参照して背景色をスプライトに入力する関数
    void setBackColor(RGB rgb) {
      sprite.fillScreen(getColor(rgb.r, rgb.g, rgb.b));
    }

    // RGBが同じことを確かめる関数
    bool compareRGBEqual(struct RGB a, struct RGB b) {
      return a.r == b.r &&
             a.g == b.g &&
             a.b == b.b;
    }

    // 計測結果をスプライトに入力する関数
    void setMeasurement(int tvoc, int eco2, float pressure, float temperature, float humidity) {
      sprite.setCursor(0, 10);
      sprite.printf("Pres.: %4.1f hPa", pressure);
      sprite.setCursor(0, 40);
      sprite.printf("Temp: %2.1f 'C %2.1f %c ", temperature, humidity, '%');
      sprite.setCursor(0, 70);
      sprite.printf("TVOC: %4d ppb ", tvoc);
      sprite.setCursor(0, 100);
      sprite.printf("eCO2: %4d ppm ", eco2);
    }

    // eCO2の値が前回計測したときから下回っていないことを確かめる関数
    bool compareEco2Value(int comparisonValue[]) {
      for (int i = 0; i < 23; i++) {
        if (comparisonValue[i] < comparisonValue[(i + 1)]) {
          return 1;
        }
      }
      return 0;
    }
};
