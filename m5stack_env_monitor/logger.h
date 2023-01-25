// 時間をtm型で取得する変数
struct tm currentDateTime;

// SHT30で計測した値の構造体
struct SensorValue {
  float temperature;
  float humidity;
  float pressure;
};

class Logger {

  public:
    Logger(ClockDial &clockDial_): clockDial(clockDial_) {

    }

    // 計測した値をSDカードに保存する関数
    void saveSensorValue(struct SensorValue latestSensorValue) {
      // ローカル時間を取得する
      boolean getTime = getLocalTime(&currentDateTime);
      String measureDay = clockDial.getDateString(currentDateTime);
      String measureTime = clockDial.getTimeString(currentDateTime);

      // SDカードに計測値を追加する
      File measureValues = SD.open("/measure_values.csv", FILE_APPEND);
      measureValues.print(
        measureDay + ","
        + measureTime + ","
        + sgp.eCO2 + ","
        + sgp.TVOC + ","
        + latestSensorValue.temperature + ","
        + latestSensorValue.humidity + ","
        + latestSensorValue.pressure + "\n"
      );
      measureValues.close();
    }

  private:
    // コンストラクタで参照先を埋める本文clockDialへの参照
    ClockDial &clockDial;

};
