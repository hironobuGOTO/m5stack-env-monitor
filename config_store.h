class ConfigStore {

  public:
    ConfigStore() {

    }

    void load() {
      // SDカードに保存された設定情報を読み込み、初期値に反映させる
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

    int getBacklightLevel() {
      return backlightLevel;
    }

    void adjustBacklightLevel(int i) {
      backlightLevel += i;
      if (backlightLevel > 5) {
        backlightLevel = 5;
      } else if (backlightLevel < 0) {
        backlightLevel = 0;
      }
      save();
    }

    boolean getBedroomMode() {
      return isBedroomMode;
    }

    void toggleBedroomMode() {
      isBedroomMode = !isBedroomMode;
      save();
    }

  private:
    // 寝室モードか
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
