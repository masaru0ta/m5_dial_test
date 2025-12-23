# M5Dial Common Flash Script
# ビルドしたファームウェアをM5Dialデバイスに書き込みます
# 使い方: .\common-flash.ps1 -ProjectPath "D:\AI\code\m5-digital\your-project" -Port COM10 -Baud 115200

param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectPath,
    [string]$Port = "COM10",
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

Write-Host "=== M5Dial Common Flash Script ===" -ForegroundColor Cyan

# パスの設定
$IDF_PATH = "D:\esp\esp-idf-v5.1.3"
$PROJECT_PATH = (Resolve-Path $ProjectPath).Path
$BUILD_PATH = "$PROJECT_PATH\build"
$ESPTOOL = "$IDF_PATH\components\esptool_py\esptool\esptool.py"
$PYTHON = "C:\Users\masar\.espressif\python_env\idf5.1_py3.10_env\Scripts\python.exe"

# プロジェクト名を取得（ディレクトリ名から）
$PROJECT_NAME = Split-Path -Leaf $PROJECT_PATH

Write-Host "Project: $PROJECT_NAME" -ForegroundColor Cyan

# ビルドディレクトリの確認
if (-not (Test-Path $BUILD_PATH)) {
    Write-Host "ERROR: Build directory not found at $BUILD_PATH" -ForegroundColor Red
    Write-Host "Please run common-build.ps1 first." -ForegroundColor Red
    exit 1
}

# 必要なファイルの確認
$bootloader = "$BUILD_PATH\bootloader\bootloader.bin"
$partition = "$BUILD_PATH\partition_table\partition-table.bin"

# アプリケーションバイナリを自動検出
# まずディレクトリ名と同じ名前を試す
$app = "$BUILD_PATH\$PROJECT_NAME.bin"

if (-not (Test-Path $app)) {
    # 見つからない場合、buildディレクトリ直下の.binファイルを検索
    $binFiles = Get-ChildItem -Path $BUILD_PATH -Filter "*.bin" -File | Where-Object { $_.Name -ne "project_description.bin" }

    if ($binFiles.Count -eq 1) {
        $app = $binFiles[0].FullName
        Write-Host "Auto-detected binary: $($binFiles[0].Name)" -ForegroundColor Yellow
    } elseif ($binFiles.Count -gt 1) {
        Write-Host "Found multiple .bin files in build directory:" -ForegroundColor Yellow
        $binFiles | ForEach-Object { Write-Host "  - $($_.Name)" -ForegroundColor Yellow }
        # 最も大きいファイルをアプリケーションバイナリとして選択
        $app = ($binFiles | Sort-Object Length -Descending | Select-Object -First 1).FullName
        Write-Host "Using largest binary: $(Split-Path -Leaf $app)" -ForegroundColor Yellow
    }
}

if (-not (Test-Path $bootloader)) {
    Write-Host "ERROR: Bootloader binary not found at $bootloader" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $partition)) {
    Write-Host "ERROR: Partition table not found at $partition" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $app)) {
    Write-Host "ERROR: Application binary not found" -ForegroundColor Red
    Write-Host "Searched for: $BUILD_PATH\$PROJECT_NAME.bin" -ForegroundColor Yellow
    Write-Host "Also searched for any .bin file in build directory" -ForegroundColor Yellow
    exit 1
}

Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Baud Rate: $Baud" -ForegroundColor Yellow
Write-Host "Flashing firmware to M5Dial..." -ForegroundColor Yellow

# esptoolで書き込み
& $PYTHON $ESPTOOL `
    --chip esp32s3 `
    --port $Port `
    --baud $Baud `
    --before default_reset `
    --after hard_reset `
    write_flash `
    --flash_mode dio `
    --flash_freq 80m `
    --flash_size 8MB `
    0x0 $bootloader `
    0x8000 $partition `
    0x10000 $app

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== Flash Successful ===" -ForegroundColor Green
    Write-Host "Your M5Dial device is now running: $PROJECT_NAME" -ForegroundColor Green
} else {
    Write-Host "`n=== Flash Failed ===" -ForegroundColor Red
    Write-Host "Please check:" -ForegroundColor Yellow
    Write-Host "  1. M5Dial is connected to $Port" -ForegroundColor Yellow
    Write-Host "  2. No other program is using the serial port" -ForegroundColor Yellow
    Write-Host "  3. Try pressing the encoder button while connecting" -ForegroundColor Yellow
    exit 1
}
