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
    Logger () {

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

  private:

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
};
