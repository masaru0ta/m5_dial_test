#!/bin/bash
# M5Dial Common Build Script (Bash wrapper)
# 使い方: ./build.sh <project-directory>
# 例: ./build.sh m5dial-hello

if [ -z "$1" ]; then
    echo "使い方: ./build.sh <project-directory>"
    echo "例: ./build.sh m5dial-hello"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$1"

# 相対パスの場合は絶対パスに変換
if [[ ! "$PROJECT_DIR" = /* ]]; then
    PROJECT_DIR="$SCRIPT_DIR/$PROJECT_DIR"
fi

PROJECT_DIR_WIN=$(cygpath -w "$PROJECT_DIR")
SCRIPT_DIR_WIN=$(cygpath -w "$SCRIPT_DIR")

echo "=== Running Build Script ==="
echo "Project: $PROJECT_DIR"
echo ""

powershell.exe -ExecutionPolicy Bypass -File "$SCRIPT_DIR_WIN\\common-build.ps1" -ProjectPath "$PROJECT_DIR_WIN"
