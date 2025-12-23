#!/bin/bash
# M5Dial Hello World Flash Script (Bash wrapper for Windows)
# Git BashからPowerShellスクリプトを実行します

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPT_DIR_WIN=$(cygpath -w "$SCRIPT_DIR")

# デフォルトのCOMポート
PORT="${1:-COM10}"
BAUD="${2:-115200}"

echo "=== Running Flash Script ==="
echo "Port: $PORT"
echo "Baud Rate: $BAUD"
echo ""

powershell.exe -ExecutionPolicy Bypass -File "$SCRIPT_DIR_WIN\\flash.ps1" -Port "$PORT" -Baud "$BAUD"
exit_code=$?

if [ $exit_code -eq 0 ]; then
    echo ""
    echo "Flash completed successfully!"
else
    echo ""
    echo "Flash failed with exit code $exit_code"
fi

exit $exit_code
