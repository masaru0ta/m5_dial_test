# M5Dial Common Build Script
# ESP-IDF環境をセットアップしてプロジェクトをビルドします
# 使い方: .\common-build.ps1 -ProjectPath "D:\AI\code\m5-digital\your-project"

param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectPath
)

$ErrorActionPreference = "Stop"

Write-Host "=== M5Dial Common Build Script ===" -ForegroundColor Cyan

# ESP-IDFのパス
$IDF_PATH = "D:\esp\esp-idf-v5.1.3"

# プロジェクトパスを絶対パスに変換
$PROJECT_PATH = (Resolve-Path $ProjectPath).Path

# ESP-IDFの存在確認
if (-not (Test-Path "$IDF_PATH\export.ps1")) {
    Write-Host "ERROR: ESP-IDF not found at $IDF_PATH" -ForegroundColor Red
    exit 1
}

# プロジェクトディレクトリの存在確認
if (-not (Test-Path $PROJECT_PATH)) {
    Write-Host "ERROR: Project directory not found at $PROJECT_PATH" -ForegroundColor Red
    exit 1
}

# CMakeLists.txtの存在確認（ESP-IDFプロジェクトかどうか）
if (-not (Test-Path "$PROJECT_PATH\CMakeLists.txt")) {
    Write-Host "ERROR: CMakeLists.txt not found in $PROJECT_PATH" -ForegroundColor Red
    Write-Host "This does not appear to be an ESP-IDF project." -ForegroundColor Red
    exit 1
}

Write-Host "Project: $PROJECT_PATH" -ForegroundColor Cyan
Write-Host "Setting up ESP-IDF environment..." -ForegroundColor Yellow
Set-Location $PROJECT_PATH

# MSYSTEMを削除（MSys/Mingwチェックを回避）
Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue

# ESP-IDF環境変数を手動設定
$env:IDF_PATH = $IDF_PATH
$env:IDF_TOOLS_PATH = "C:\Users\masar\.espressif"

# PATHに必要なツールを追加
$IDF_PYTHON_ENV = "C:\Users\masar\.espressif\python_env\idf5.1_py3.10_env"
$IDF_TOOLS_DIR = "C:\Users\masar\.espressif\tools"
$CMAKE_PATH = "$IDF_TOOLS_DIR\cmake\3.24.0\bin"
$NINJA_PATH = "$IDF_TOOLS_DIR\ninja\1.10.2"
$XTENSA_PATH = "$IDF_TOOLS_DIR\xtensa-esp32s3-elf\esp-12.2.0_20230208\xtensa-esp32s3-elf\bin"
$CCACHE_PATH = "$IDF_TOOLS_DIR\ccache\4.8\ccache-4.8-windows-x86_64"

$env:PATH = "$IDF_PYTHON_ENV\Scripts;$CMAKE_PATH;$NINJA_PATH;$XTENSA_PATH;$CCACHE_PATH;$IDF_PATH\tools;$env:PATH"

Write-Host "Building project..." -ForegroundColor Yellow

# Pythonとidf.pyの直接パス
$PYTHON_EXE = "$IDF_PYTHON_ENV\Scripts\python.exe"
$IDF_PY = "$IDF_PATH\tools\idf.py"

# ビルド実行
Write-Host "Executing: $PYTHON_EXE $IDF_PY build" -ForegroundColor Gray
& $PYTHON_EXE $IDF_PY build

# ビルドディレクトリが存在するか確認
if (Test-Path "$PROJECT_PATH\build") {
    if ($LASTEXITCODE -eq 0) {
        Write-Host "`n=== Build Successful ===" -ForegroundColor Green
        Write-Host "Binary files are located in: $PROJECT_PATH\build" -ForegroundColor Green
    } else {
        Write-Host "`n=== Build Completed with Warnings ===" -ForegroundColor Yellow
        Write-Host "Binary files are located in: $PROJECT_PATH\build" -ForegroundColor Green
    }
} else {
    Write-Host "`n=== Build Failed - No build directory created ===" -ForegroundColor Red
    Write-Host "LASTEXITCODE was: $LASTEXITCODE" -ForegroundColor Red
    exit 1
}
