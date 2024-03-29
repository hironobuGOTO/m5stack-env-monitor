#include <WiFi.h>
#include "config.h"

class ClockDial {

  public:
    ClockDial() {

    }

    //Wifi接続し、M5Stackの時計を補正する関数as
    void correctTime() {
      // Wi-Fiを起動してntpから現在時刻を取得する
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

      // Wi-Fi接続が完了するのを待つ
      M5.Lcd.printf("\n  Connecting");
      while (WiFi.status() != WL_CONNECTED) {
        M5.Lcd.printf(".");
        delay(1000);
      }
      // 時刻の補正
      configTime(9 * 3600L, 0, "ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp");
    }

    // tm オブジェクトから日付文字列 (例: "12/1") を返す関数
    String getDateString(struct tm currentDateTime) {
      int month = currentDateTime.tm_mon + 1;
      String dateString = String(month) + "/" + String(currentDateTime.tm_mday);
      Serial.println(dateString);
      return dateString;
    }

    // tm オブジェクトから時刻文字列 (例: "00:00") を返す関数
    static String getTimeString(struct tm currentDateTime) {
      String hour;
      String minutes;

      if (currentDateTime.tm_hour < 10) {
        hour = "0" + String(currentDateTime.tm_hour);
      } else {
        hour = String(currentDateTime.tm_hour);
      }
      if (currentDateTime.tm_min < 10) {
        minutes = "0" + String(currentDateTime.tm_min);
      } else {
        minutes = String(currentDateTime.tm_min);
      }
      String timeString = hour + ":" + minutes;
      Serial.println(timeString);
      return timeString;
    }

    // tm オブジェクトから曜日を返す関数
    const String getWdayString(struct tm currentDateTime) {
      const String WDAY_ARRAY[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      return WDAY_ARRAY[currentDateTime.tm_wday];
    }

    // tm オブジェクトから年月日を返す関数
    void getDateString(struct tm currentDateTime, int* year, int* month, int* day) {
      *year = currentDateTime.tm_year + 1900;
      *month = currentDateTime.tm_mon + 1;
      *day = currentDateTime.tm_mday;
    }

  private:

};
