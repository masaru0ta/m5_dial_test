#!/bin/bash
# M5Dial Common Flash Script (Bash wrapper)
# 使い方: ./flash.sh <project-directory> [port] [baud]
# 例: ./flash.sh m5dial-hello COM10 115200

if [ -z "$1" ]; then
    echo "使い方: ./flash.sh <project-directory> [port] [baud]"
    echo "例: ./flash.sh m5dial-hello COM10 115200"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$1"
PORT="${2:-COM10}"
BAUD="${3:-115200}"

# 相対パスの場合は絶対パスに変換
if [[ ! "$PROJECT_DIR" = /* ]]; then
    PROJECT_DIR="$SCRIPT_DIR/$PROJECT_DIR"
fi

PROJECT_DIR_WIN=$(cygpath -w "$PROJECT_DIR")
SCRIPT_DIR_WIN=$(cygpath -w "$SCRIPT_DIR")

echo "=== Running Flash Script ==="
echo "Project: $PROJECT_DIR"
echo "Port: $PORT"
echo "Baud Rate: $BAUD"
echo ""

powershell.exe -ExecutionPolicy Bypass -File "$SCRIPT_DIR_WIN\\common-flash.ps1" -ProjectPath "$PROJECT_DIR_WIN" -Port "$PORT" -Baud "$BAUD"
