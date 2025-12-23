# M5Dial Hello World Flash Script
# ビルドしたファームウェアをM5Dialデバイスに書き込みます

param(
    [string]$Port = "COM10",
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

Write-Host "=== M5Dial Hello World Flash Script ===" -ForegroundColor Cyan

# パスの設定
$IDF_PATH = "D:\esp\esp-idf-v5.1.3"
$PROJECT_PATH = "D:\AI\code\m5-digital\m5dial-hello"
$BUILD_PATH = "$PROJECT_PATH\build"
$ESPTOOL = "$IDF_PATH\components\esptool_py\esptool\esptool.py"
$PYTHON = "C:\Users\masar\.espressif\python_env\idf5.1_py3.10_env\Scripts\python.exe"

# ビルドディレクトリの確認
if (-not (Test-Path $BUILD_PATH)) {
    Write-Host "ERROR: Build directory not found. Please run build.ps1 first." -ForegroundColor Red
    exit 1
}

# 必要なファイルの確認
$bootloader = "$BUILD_PATH\bootloader\bootloader.bin"
$partition = "$BUILD_PATH\partition_table\partition-table.bin"
$app = "$BUILD_PATH\m5dial-hello.bin"

if (-not (Test-Path $bootloader)) {
    Write-Host "ERROR: Bootloader binary not found" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $partition)) {
    Write-Host "ERROR: Partition table not found" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $app)) {
    Write-Host "ERROR: Application binary not found" -ForegroundColor Red
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
    Write-Host "Your M5Dial device should now be running the Hello World firmware!" -ForegroundColor Green
} else {
    Write-Host "`n=== Flash Failed ===" -ForegroundColor Red
    Write-Host "Please check:" -ForegroundColor Yellow
    Write-Host "  1. M5Dial is connected to $Port" -ForegroundColor Yellow
    Write-Host "  2. No other program is using the serial port" -ForegroundColor Yellow
    Write-Host "  3. Try pressing the encoder button while connecting" -ForegroundColor Yellow
    exit 1
}
