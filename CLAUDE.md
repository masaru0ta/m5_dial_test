# CLAUDE.md - M5Dial開発プロジェクトガイド

> **⚠️ 最重要指示（必ず遵守すること）**
>
> 1. **回答は必ず日本語で行うこと**
> 2. **ソースコードのコメントは全て日本語で書くこと**
> 3. **Gitのコミットメッセージは全て日本語で書くこと**

このドキュメントは、Claude（AI）がこのプロジェクトで作業する際のリファレンスです。

## プロジェクト概要

**目的**: M5Stack M5Dial (ESP32-S3ベース) 用のファームウェア開発
**ビルドシステム**: ESP-IDF v5.1.3
**開発環境**: Windows + Git Bash
**デバイス接続**: COM10 (デフォルト)

## ディレクトリ構造

```
D:\AI\code\m5-digital\
├── common-build.ps1              # 共通ビルドスクリプト
├── common-flash.ps1              # 共通フラッシュスクリプト
├── common-build-and-flash.ps1    # 共通ビルド&フラッシュスクリプト
├── build.sh                      # Bashラッパー（ビルド）
├── flash.sh                      # Bashラッパー（フラッシュ）
├── build-and-flash.sh            # Bashラッパー（ビルド&フラッシュ）
├── BUILD-SYSTEM-README.md        # ユーザー向けドキュメント
├── CLAUDE.md                     # このファイル（Claude向けガイド）
├── m5dial-hello/                 # Hello Worldサンプルプロジェクト
├── M5Dial-UserDemo/              # M5Stack公式デモ
└── (今後作成するプロジェクト)/
```

## 共通スクリプトの使い方（Claude向け）

### ビルドとフラッシュを一度に実行（最も推奨）

```bash
cd D:\AI\code\m5-digital
./build-and-flash.sh <プロジェクト名> COM10 115200
```

**例**:
```bash
./build-and-flash.sh m5dial-hello COM10 115200
```

### ビルドのみ実行

```bash
./build.sh <プロジェクト名>
```

### フラッシュのみ実行（ビルド済みの場合）

```bash
./flash.sh <プロジェクト名> COM10 115200
```

### 実行時の注意

1. **必ずBashツールを使う** - PowerShellスクリプトを直接Bashで実行しない
2. **相対パスで指定** - プロジェクト名のみ指定（例: `m5dial-hello`）
3. **パラメータの順序** - `<プロジェクト名> [ポート] [ボーレート]`
4. **デフォルト値** - ポートとボーレートは省略可能（COM10, 115200がデフォルト）

## 新しいプロジェクトの作成手順

### 1. プロジェクトディレクトリ構造を作成

```bash
cd D:\AI\code\m5-digital
mkdir my-new-project
cd my-new-project
mkdir main
```

### 2. 必須ファイルを作成

#### CMakeLists.txt (プロジェクトルート)
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my-new-project)
```

#### main/CMakeLists.txt
```cmake
idf_component_register(SRCS "main.cpp"
                      INCLUDE_DIRS ".")
```

#### sdkconfig.defaults
```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

#### main/main.cpp
```cpp
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void)
{
    printf("Hello from my-new-project!\n");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 3. ビルド&フラッシュ

```bash
cd D:\AI\code\m5-digital
./build-and-flash.sh my-new-project COM10
```

## 重要な注意点

### ⚠️ ビルドスクリプトに関する重要事項

1. **MSYSTEM環境変数の問題**
   - Git Bash環境では`MSYSTEM`環境変数が設定されている
   - ESP-IDFの`export.ps1`はMSys/Mingw環境を拒否する
   - **解決策**: `common-build.ps1`内で`Remove-Item Env:\MSYSTEM`を実行
   - この処理を削除すると「MSys/Mingw is not supported」エラーが発生

2. **PATHの明示的な設定**
   - ESP-IDFツールへのパスを明示的に設定する必要がある
   - 必要なツール: cmake, ninja, xtensa-esp32s3-elf, ccache, python
   - `export.ps1`に依存せず、直接PATHを構築

3. **プロジェクト名とバイナリ名の一致**
   - プロジェクトディレクトリ名 = ビルドされるバイナリファイル名
   - 例: `m5dial-hello` → `build/m5dial-hello.bin`
   - `common-flash.ps1`はこの命名規則に依存

4. **Python実行の方法**
   - `idf.py`コマンドを直接実行するのではなく、Pythonで実行
   - 実行方法: `python.exe $IDF_PATH\tools\idf.py build`

### ⚠️ フラッシュに関する注意

1. **シリアルポートの確認**
   - デフォルト: COM10
   - ユーザーに確認してから実行

2. **書き込みモード**
   - M5Dialは通常、書き込みモードへの手動移行は不要
   - エラーが発生した場合のみ、エンコーダーボタンを押しながら接続

3. **フラッシュアドレス**
   - 0x0: bootloader.bin
   - 0x8000: partition-table.bin
   - 0x10000: アプリケーションバイナリ

## ESP-IDF環境の詳細

### インストールパス

- **ESP-IDF**: `D:\esp\esp-idf-v5.1.3`
- **ESP-IDFツール**: `C:\Users\masar\.espressif`
- **Python環境**: `C:\Users\masar\.espressif\python_env\idf5.1_py3.10_env`

### ツールバージョン

- CMake: 3.24.0
- Ninja: 1.10.2
- Xtensa toolchain: esp-12.2.0_20230208
- ccache: 4.8

### ESP-IDF設定

```powershell
$IDF_PATH = "D:\esp\esp-idf-v5.1.3"
$IDF_TOOLS_PATH = "C:\Users\masar\.espressif"
$IDF_PYTHON_ENV = "C:\Users\masar\.espressif\python_env\idf5.1_py3.10_env"
```

## トラブルシューティング

### ビルドエラー: "MSys/Mingw is not supported"

**原因**: Git Bashの`MSYSTEM`環境変数がESP-IDFと競合
**解決**: `common-build.ps1`で自動的に削除済み
**対処不要**: このエラーは発生しないはず

### ビルドエラー: "cmake must be available on the PATH"

**原因**: ESP-IDFツールへのPATHが設定されていない
**解決**: `common-build.ps1`で自動的に設定済み
**対処不要**: このエラーは発生しないはず

### ビルドエラー: "idf.py not found"

**原因**: idf.pyコマンドがPATHにない
**解決**: `common-build.ps1`はPythonで直接実行するため問題なし
**対処不要**: このエラーは発生しないはず

### フラッシュエラー: "Build directory not found"

**原因**: ビルドしていない、またはビルド失敗
**解決**: 先に`./build.sh <プロジェクト名>`を実行

### フラッシュエラー: "Application binary not found"

**原因**: プロジェクト名とバイナリ名が不一致
**確認**: プロジェクトディレクトリ名 = CMakeLists.txtのproject名
**解決**: プロジェクト名を確認し、正しい名前で実行

### フラッシュエラー: "Failed to connect to ESP32-S3"

**原因**:
1. M5Dialが接続されていない
2. 他のプログラムがポートを使用中
3. COMポート番号が間違っている

**解決**:
1. USB接続を確認
2. シリアルモニタを閉じる
3. デバイスマネージャーでCOMポート確認
4. エンコーダーボタンを押しながら接続を試す

## 過去に解決した問題（履歴）

### 問題1: export.ps1実行時のMSys/Mingwエラー

**症状**:
```
ERROR: MSys/Mingw is not supported. Please follow the getting started guide...
```

**根本原因**: Git BashのMSYSTEM環境変数がESP-IDFスクリプトと競合

**解決策**:
```powershell
Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue
```

### 問題2: idf.pyコマンドが見つからない

**症状**:
```
idf.py : 用語 'idf.py' は、コマンドレット、関数、スクリプト ファイル、または操作可能なプログラムの名前として認識されません
```

**根本原因**: export.ps1が失敗し、PATHが設定されていない

**解決策**:
- export.ps1に依存せず、直接Pythonでidf.pyを実行
- 全ツールのPATHを手動設定

### 問題3: ビルド成功表示されるが実際にはビルドされていない

**症状**: "Build Successful"と表示されるが、buildディレクトリが空

**根本原因**: idf.pyが実行されておらず、exit code 0で終了

**解決策**:
- ビルドディレクトリの存在確認を追加
- cmakeやその他のツールのPATHを明示的に設定

## M5Dial ハードウェア仕様（参考）

### MCU
- ESP32-S3 (Xtensa dual-core LX7, 240MHz)
- Flash: 8MB
- PSRAM: 8MB

### ディスプレイ
- GC9A01 LCDコントローラー
- 1.28インチ 240x240 円形IPS
- SPIインターフェース

### GPIO割り当て（m5dial-helloより）
```cpp
// LCD
#define LCD_MOSI_PIN 5
#define LCD_SCLK_PIN 6
#define LCD_DC_PIN   4
#define LCD_CS_PIN   7
#define LCD_RST_PIN  8
#define LCD_BL_PIN   9

// Rotary Encoder
#define ENCODER_A_PIN   41
#define ENCODER_B_PIN   40
#define ENCODER_BTN_PIN 42
```

## サンプルプロジェクト

### m5dial-hello

**場所**: `D:\AI\code\m5-digital\m5dial-hello`

**機能**:
- 円形LCDに「Hello World」テキスト表示
- ロータリーエンコーダーでカウンター変更
- ボタン押下でカウンターリセット

**ビルド&フラッシュ**:
```bash
cd D:\AI\code\m5-digital
./build-and-flash.sh m5dial-hello COM10
```

**使用ライブラリ**: LovyanGFX (ESP-IDFコンポーネント)

## まとめ：Claudeが作業する際のチェックリスト

新しいプロジェクトをビルド&フラッシュする際：

1. [ ] プロジェクトディレクトリ構造を確認（CMakeLists.txt, main/, sdkconfig.defaults）
2. [ ] プロジェクト名がディレクトリ名と一致しているか確認
3. [ ] `D:\AI\code\m5-digital`ディレクトリに移動
4. [ ] `./build-and-flash.sh <プロジェクト名> COM10`を実行
5. [ ] ビルドエラーが発生した場合、エラーメッセージを確認
6. [ ] フラッシュエラーが発生した場合、COM10が正しいか確認

---

**最終更新**: 2025-12-23
**ESP-IDF バージョン**: v5.1.3
**対象デバイス**: M5Stack M5Dial (ESP32-S3)
