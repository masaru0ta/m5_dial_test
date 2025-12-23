# M5Dial ビルドシステム

M5Dial用のESP-IDFプロジェクトを簡単にビルド・書き込みできる共通スクリプトシステムです。

## 概要

このディレクトリには、M5Dial向けのESP-IDFプロジェクトをビルド・フラッシュするための共通スクリプトが含まれています。
新しいプロジェクトを作成するたびにビルドスクリプトをコピーする必要はありません。

## ファイル構成

```
D:\AI\code\m5-digital\
├── common-build.ps1              # 共通ビルドスクリプト (PowerShell)
├── common-flash.ps1              # 共通フラッシュスクリプト (PowerShell)
├── common-build-and-flash.ps1    # 共通ビルド&フラッシュスクリプト (PowerShell)
├── build.sh                      # ビルドスクリプト (Bash wrapper)
├── flash.sh                      # フラッシュスクリプト (Bash wrapper)
├── build-and-flash.sh            # ビルド&フラッシュスクリプト (Bash wrapper)
├── BUILD-SYSTEM-README.md        # このファイル
├── m5dial-hello/                 # サンプルプロジェクト
└── (その他のプロジェクト)/
```

## 使い方

### Git Bashから使用する場合（推奨）

#### 1. プロジェクトをビルドのみ

```bash
cd D:\AI\code\m5-digital
./build.sh m5dial-hello
```

#### 2. ビルド済みのプロジェクトをフラッシュのみ

```bash
cd D:\AI\code\m5-digital
./flash.sh m5dial-hello COM10 115200
```

#### 3. ビルドとフラッシュを一度に実行（最も便利）

```bash
cd D:\AI\code\m5-digital
./build-and-flash.sh m5dial-hello COM10 115200
```

#### パラメータ

- `<project-directory>`: プロジェクトディレクトリ名（例: `m5dial-hello`）
- `[port]`: シリアルポート（省略時: `COM10`）
- `[baud]`: ボーレート（省略時: `115200`）

### PowerShellから直接使用する場合

```powershell
cd D:\AI\code\m5-digital

# ビルドのみ
.\common-build.ps1 -ProjectPath "D:\AI\code\m5-digital\m5dial-hello"

# フラッシュのみ
.\common-flash.ps1 -ProjectPath "D:\AI\code\m5-digital\m5dial-hello" -Port COM10 -Baud 115200

# ビルド&フラッシュ
.\common-build-and-flash.ps1 -ProjectPath "D:\AI\code\m5-digital\m5dial-hello" -Port COM10 -Baud 115200
```

## 新しいプロジェクトの作成方法

1. **プロジェクトディレクトリを作成**

```bash
cd D:\AI\code\m5-digital
mkdir my-new-project
cd my-new-project
```

2. **ESP-IDFプロジェクトの基本構造を作成**

```
my-new-project/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.cpp (または main.c)
```

3. **共通スクリプトでビルド&フラッシュ**

```bash
cd D:\AI\code\m5-digital
./build-and-flash.sh my-new-project COM10
```

## 必須条件

- ESP-IDF v5.1.3 が `D:\esp\esp-idf-v5.1.3` にインストールされていること
- ESP-IDFツールが `C:\Users\masar\.espressif` にインストールされていること
- M5DialがシリアルポートCOM10（または指定したポート）に接続されていること

## トラブルシューティング

### ビルドエラー: "MSys/Mingw is not supported"

- このエラーは自動的に回避されます（`common-build.ps1`内で`MSYSTEM`環境変数を削除）

### フラッシュエラー: "Build directory not found"

- 先に`./build.sh <project>`を実行してビルドしてください

### フラッシュエラー: "Application binary not found"

- プロジェクト名とバイナリファイル名が一致しているか確認してください
- プロジェクトディレクトリ名 = バイナリファイル名（例: `m5dial-hello` → `m5dial-hello.bin`）

### シリアルポート接続エラー

1. M5DialがUSBケーブルで接続されているか確認
2. 他のプログラム（シリアルモニタなど）がポートを使用していないか確認
3. デバイスマネージャーで正しいCOMポート番号を確認
4. 書き込み時にエンコーダーボタンを押しながら接続してみる

## サンプルプロジェクト

### m5dial-hello

シンプルなHello Worldプロジェクト：
- 円形LCDに「Hello World」を表示
- ロータリーエンコーダーでカウンター変更
- ボタン押下でカウンターリセット

ビルド&フラッシュ:
```bash
./build-and-flash.sh m5dial-hello COM10
```

## スクリプトの内部動作

### common-build.ps1の主な処理

1. ESP-IDF環境変数の設定（`IDF_PATH`, `IDF_TOOLS_PATH`）
2. `MSYSTEM`環境変数の削除（MSys/Mingwチェック回避）
3. 必要なツールのPATH設定（cmake, ninja, xtensa-esp32s3-elf, ccache）
4. `python.exe idf.py build`の実行

### common-flash.ps1の主な処理

1. ビルドディレクトリの確認
2. バイナリファイルの存在確認（bootloader.bin, partition-table.bin, <project-name>.bin）
3. esptool.pyでESP32-S3にフラッシュ
   - 0x0: bootloader
   - 0x8000: partition table
   - 0x10000: application

## ライセンス

このビルドシステムは自由に使用・改変できます。
