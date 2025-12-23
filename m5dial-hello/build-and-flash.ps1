# M5Dial Hello World Build and Flash Script
# ビルドと書き込みを連続で実行します

param(
    [string]$Port = "COM10",
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

Write-Host "=== M5Dial Hello World Build and Flash Script ===" -ForegroundColor Cyan
Write-Host ""

# ビルドスクリプトのパス
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$BUILD_SCRIPT = Join-Path $SCRIPT_DIR "build.ps1"
$FLASH_SCRIPT = Join-Path $SCRIPT_DIR "flash.ps1"

# ビルド実行
Write-Host "Step 1/2: Building project..." -ForegroundColor Cyan
& $BUILD_SCRIPT

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed. Aborting." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Step 2/2: Flashing to device..." -ForegroundColor Cyan
& $FLASH_SCRIPT -Port $Port -Baud $Baud

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== All Done! ===" -ForegroundColor Green
} else {
    Write-Host "`n=== Flash failed ===" -ForegroundColor Red
    exit 1
}
