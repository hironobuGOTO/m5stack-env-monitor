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

const int MAX_GRAPH_VALUE = 1500;
const int MAX_GRAPH_HEIGHT = 100;

class SpriteManager {

  public:
    // コンストラクタで引数として本文configStoreインスタンスからconfigStore_として取得する
    // コロンの後のconfigStoreはprivate領域のインスタンスへの参照を表す
    SpriteManager(ConfigStore &configStore_, ClockDial &clockDial_): configStore(configStore_), clockDial(clockDial_) {
      // グラフ表示用のキューを初期化する関数を呼び出し
      initializeEco2GraphValueList();
    };
    // スプライトの作成
    void initializeSprite() {
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
        discomfortStatusColor = calcDiscomfortColor(latestSensorValue);
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

      // eCO2濃度をグラフ化してスプライトに入力
      setMeasureValueGraph();

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

    // 時間をtm型で取得する変数
    struct tm currentDateTime;

    // コンストラクタで参照先を埋める本文clockDialへの参照
    ClockDial &clockDial;

    // コンストラクタで参照先を埋める本文configStoreへの参照
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
    boolean compareRGBEqual(struct RGB a, struct RGB b) {
      return a.r == b.r &&
             a.g == b.g &&
             a.b == b.b;
    }

    // ローカル時間を取得する関数
    String setClockTime() {
      boolean getTime = getLocalTime(&currentDateTime);
      String measureTime = clockDial.getTimeString(currentDateTime);
      return measureTime;
    }

    // 計測結果をスプライトに入力する関数
    void setMeasurement(int tvoc, int eco2, float pressure, float temperature, float humidity) {
      const String TIME_STRING = setClockTime();
      int year, month, day;
      clockDial.getDateString(currentDateTime, &year, &month, &day);
      sprite.setTextFont(1);
      sprite.setTextSize(2);
      sprite.setCursor(15, 10);
      sprite.printf("%s", clockDial.getWdayString(currentDateTime));
      sprite.setCursor(90, 10);
      sprite.printf("%4d/%2d/%2d", year, month, day);
      sprite.setCursor(250, 10);
      sprite.printf("%s", TIME_STRING);
      sprite.setTextSize(1);
      sprite.setTextFont(7);
      sprite.setCursor(100, 40);
      sprite.printf("%4d", eco2);
      sprite.setTextFont(4);
      sprite.setCursor(255, 65);
      sprite.printf("ppm");
      sprite.setTextFont(4);
      sprite.setCursor(10, 105);
      sprite.printf("%2.1f 'C", temperature);
      sprite.setCursor(98, 105);
      sprite.printf("%2.1f %c", humidity, '%');
      sprite.setCursor(180, 105);
      sprite.printf("%4.1f hPa", pressure);
      sprite.drawFastHLine(0, 31, 320, TFT_WHITE);
      sprite.drawFastHLine(0, 98, 320, TFT_WHITE);
      sprite.drawFastHLine(0, 132, 320, TFT_WHITE);
      sprite.drawFastVLine(70, 0, 31, TFT_WHITE);
      sprite.drawFastVLine(230, 0, 31, TFT_WHITE);
      sprite.drawFastVLine(94, 98, 34, TFT_WHITE);
      sprite.drawFastVLine(178, 98, 34, TFT_WHITE);
    }

    // eCO2の値が前回計測したときから下回っていないことを確かめる関数
    boolean compareEco2Value(int comparisonValue[]) {
      for (int i = 0; i < 23; i++) {
        if (comparisonValue[i] < comparisonValue[(i + 1)]) {
          return 1;
        }
      }
      return 0;
    }

    // 不快指数を計算して、表示する色を返す関数
    RGB calcDiscomfortColor(struct SensorValue sensorValue) {
      // 不快指数の計算
      const float DISCOMFORT_INDEX = (
                                       (0.81 * sensorValue.temperature)
                                       + ((0.01 * sensorValue.humidity)
                                          * ((0.99 * sensorValue.temperature) - 14.3))
                                       + 46.3
                                     );
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
      return discomfortStatusColor;
    }

    // グラフをスプライトに入力する関数
    void setMeasureValueGraph() {
      // 過去のキューに入れたeCO2値をグラフに描写
      for (int i = 0; i <= 23; i++) {
        int eco2Value = 0;
        int graphHeightEco2 = 0;
        boolean peekIndex = eco2GraphValueList.peekIdx(&eco2Value, i);

        //eCO2がグラフの最大値を超えたときグラフ最大値を超えないようにする
        if (eco2Value > MAX_GRAPH_VALUE) {
          graphHeightEco2 = MAX_GRAPH_HEIGHT;
        } else {
          graphHeightEco2 = map(eco2Value, 0, MAX_GRAPH_VALUE, 1, MAX_GRAPH_HEIGHT);
        }
        // グラフにeCO2値に応じた色付けする
        if (graphHeightEco2 < 67) {
          sprite.fillRect((i * 13), (240 - graphHeightEco2), 13, graphHeightEco2, TFT_GREEN);
        } else if (graphHeightEco2 < 99) {
          sprite.fillRect((i * 13), (240 - graphHeightEco2), 13, graphHeightEco2, TFT_YELLOW);
        } else {
          sprite.fillRect((i * 13), (240 - graphHeightEco2), 13, graphHeightEco2, TFT_RED);
        }
      }
      // 現在のeCO2値をグラフに描写
      const int LATEST_GRAPH_HEIGHT_ECO2 = map((int)sgp.eCO2, 0, MAX_GRAPH_VALUE, 1, MAX_GRAPH_HEIGHT);
      if (LATEST_GRAPH_HEIGHT_ECO2 < 67) {
        sprite.fillRect(299, (240 - LATEST_GRAPH_HEIGHT_ECO2), 21, LATEST_GRAPH_HEIGHT_ECO2, TFT_GREEN);
      } else if (LATEST_GRAPH_HEIGHT_ECO2 < 99) {
        sprite.fillRect(299, (240 - LATEST_GRAPH_HEIGHT_ECO2), 21, LATEST_GRAPH_HEIGHT_ECO2, TFT_YELLOW);
      } else {
        sprite.fillRect(299, (240 - LATEST_GRAPH_HEIGHT_ECO2), 21, LATEST_GRAPH_HEIGHT_ECO2, TFT_RED);
      }
    }
};
