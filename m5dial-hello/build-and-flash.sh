#!/bin/bash
# M5Dial Hello World Build and Flash Script (Bash wrapper)
# ビルドと書き込みを連続で実行します

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPT_DIR_WIN=$(cygpath -w "$SCRIPT_DIR")

# デフォルトのCOMポート
PORT="${1:-COM10}"
BAUD="${2:-115200}"

echo "=== Running Build and Flash Script ==="
echo "Port: $PORT"
echo "Baud Rate: $BAUD"
echo ""

powershell.exe -ExecutionPolicy Bypass -File "$SCRIPT_DIR_WIN\\build-and-flash.ps1" -Port "$PORT" -Baud "$BAUD"
exit_code=$?

if [ $exit_code -eq 0 ]; then
    echo ""
    echo "Build and Flash completed successfully!"
else
    echo ""
    echo "Build and Flash failed with exit code $exit_code"
fi

exit $exit_code
