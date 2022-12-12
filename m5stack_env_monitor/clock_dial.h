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
      M5.Lcd.printf("Connecting");
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
      String timeString = String(currentDateTime.tm_hour) + ":" + String(currentDateTime.tm_min);
      Serial.println(timeString);
      return timeString;
    }

  private:

};
