# M5Dial Hello World

M5Dialデバイス用のシンプルなHello Worldサンプルプログラムです。

## 機能

- **LCD表示**: 240x240の円形ディスプレイに"Hello World"を表示
- **エンコーダー操作**: ロータリーエンコーダーを回してカウンター値を変更
- **ボタン操作**: エンコーダーボタンを押してカウンターをリセット

## 必要な環境

- ESP-IDF v5.1.3 (インストール済み: `D:\esp\esp-idf-v5.1.3`)
- Git Bash (Windows)
- M5Dial デバイス

## ビルドと書き込み

### 1. ビルドのみ

```bash
./build.sh
```

### 2. 書き込みのみ

```bash
./flash.sh [COMポート] [ボーレート]
```

例:
```bash
./flash.sh COM10 115200
```

### 3. ビルド + 書き込み

```bash
./build-and-flash.sh [COMポート] [ボーレート]
```

例:
```bash
./build-and-flash.sh COM10 115200
```

## PowerShellから実行

```powershell
# ビルド
.\build.ps1

# 書き込み
.\flash.ps1 -Port COM10 -Baud 115200

# ビルド + 書き込み
.\build-and-flash.ps1 -Port COM10 -Baud 115200
```

## プロジェクト構造

```
m5dial-hello/
├── main/
│   ├── main.cpp           # メインプログラム
│   └── CMakeLists.txt     # メインコンポーネント設定
├── components/
│   └── LovyanGFX/         # ディスプレイライブラリ
├── CMakeLists.txt         # プロジェクト設定
├── sdkconfig.defaults     # ESP32-S3デフォルト設定
├── build.sh / build.ps1   # ビルドスクリプト
├── flash.sh / flash.ps1   # 書き込みスクリプト
├── build-and-flash.sh     # ビルド+書き込みスクリプト
└── README.md              # このファイル
```

## ハードウェア仕様

### M5Dial (ESP32-S3)

- **MCU**: ESP32-S3
- **Display**: GC9A01, 240x240 円形LCD
- **Encoder**: ロータリーエンコーダー (GPIO40, GPIO41)
- **Button**: エンコーダープッシュボタン (GPIO42)
- **Flash**: 8MB

### ピン配置

| 機能 | GPIO |
|------|------|
| LCD MOSI | 5 |
| LCD SCLK | 6 |
| LCD DC | 4 |
| LCD CS | 7 |
| LCD RST | 8 |
| LCD BL | 9 |
| Encoder A | 41 |
| Encoder B | 40 |
| Encoder Button | 42 |

## カスタマイズ

### 表示内容を変更する

`main/main.cpp` の `update_display()` 関数を編集してください。

### エンコーダー感度を調整

`main/main.cpp` のメインループ内の `vTaskDelay()` の値を変更してください。

### 画面の明るさを変更

`main/main.cpp` の `display.setBrightness(128)` の値を変更してください (0-255)。

## トラブルシューティング

### ビルドエラー

```bash
# クリーンビルド
rm -rf build
./build.sh
```

### 書き込みエラー

- COMポートが正しいか確認
- 他のプログラムがポートを使用していないか確認
- エンコーダーボタンを押しながら書き込みを試す

### シリアルモニター

デバイスのログを確認:
```bash
python -m serial.tools.miniterm COM10 115200
```

終了: `Ctrl + ]`

## ライセンス

このサンプルプロジェクトはMITライセンスです。

## 参考

- [M5Dial-UserDemo](https://github.com/m5stack/M5Dial-UserDemo)
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/)
