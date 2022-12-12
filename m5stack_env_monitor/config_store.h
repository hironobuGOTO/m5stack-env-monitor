class ConfigStore {

  public:
    ConfigStore() {
 
    }

    // SDカードに保存された設定情報を読み込み、初期値に反映させる関数
    void load() {
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

      backlightLevel = configJson["backlightLevel"];
      isBedroomMode = configJson["bedroomFlag"];
    }

    // バックライトの明度を取得する関数
    int getBacklightLevel() {
      return backlightLevel;
    }

    // バックライトの明度を変更する関数
    void adjustBacklightLevel(int i) {
      backlightLevel += i;
      if (backlightLevel > 5) {
        backlightLevel = 5;
      } else if (backlightLevel < 0) {
        backlightLevel = 0;
      }
      save();
    }

    // 寝室モードかどうかを取得する関数
    boolean getBedroomMode() {
      return isBedroomMode;
    }

    // 寝室モードかどうかを切り替える関数
    void toggleBedroomMode() {
      isBedroomMode = !isBedroomMode;
      save();
    }

  private:
    // 寝室モードかを保持する変数
    boolean isBedroomMode = false;

    // バックライトの輝度 (0〜5, 初期値は中央値)
    int backlightLevel = 2;

    // 生成したJSONをSDカードに出力する関数
    void save() {
      // configFile の定義
      File configFile = SD.open("/config.txt", FILE_WRITE);

      // 設定値を保存しておくJSON
      DynamicJsonDocument configJson(1024);

      // JSONに値を入力
      configJson["backlightLevel"] = backlightLevel;
      configJson["bedroomFlag"] = isBedroomMode;

      // JSONのシリアライズ
      String output;
      serializeJson(configJson, output);

      // SDカードへの書き込みと処理終了
      configFile.println(output);
      configFile.close();
    }
};
