# M5Dial Common Build and Flash Script
# ビルドと書き込みを連続で実行します
# 使い方: .\common-build-and-flash.ps1 -ProjectPath "D:\AI\code\m5-digital\your-project" -Port COM10 -Baud 115200

param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectPath,
    [string]$Port = "COM10",
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

Write-Host "=== M5Dial Common Build and Flash Script ===" -ForegroundColor Cyan
Write-Host ""

# スクリプトのディレクトリ
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$BUILD_SCRIPT = Join-Path $SCRIPT_DIR "common-build.ps1"
$FLASH_SCRIPT = Join-Path $SCRIPT_DIR "common-flash.ps1"

# プロジェクト名を取得
$PROJECT_PATH = (Resolve-Path $ProjectPath).Path
$PROJECT_NAME = Split-Path -Leaf $PROJECT_PATH
Write-Host "Project: $PROJECT_NAME" -ForegroundColor Cyan
Write-Host ""

# ビルド実行
Write-Host "Step 1/2: Building project..." -ForegroundColor Cyan
& $BUILD_SCRIPT -ProjectPath $PROJECT_PATH

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed. Aborting." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Step 2/2: Flashing to device..." -ForegroundColor Cyan
& $FLASH_SCRIPT -ProjectPath $PROJECT_PATH -Port $Port -Baud $Baud

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== All Done! ===" -ForegroundColor Green
    Write-Host "Build and Flash completed successfully for: $PROJECT_NAME" -ForegroundColor Green
} else {
    Write-Host "`n=== Flash failed ===" -ForegroundColor Red
    exit 1
}
