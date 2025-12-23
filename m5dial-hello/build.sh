#!/bin/bash
# M5Dial Hello World Build Script (Bash wrapper)
# Git BashからPowerShellスクリプトを実行します

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPT_DIR_WIN=$(cygpath -w "$SCRIPT_DIR")

echo "=== Running Build Script ==="
powershell.exe -ExecutionPolicy Bypass -File "$SCRIPT_DIR_WIN\\build.ps1"
exit_code=$?

if [ $exit_code -eq 0 ]; then
    echo ""
    echo "Build completed successfully!"
else
    echo ""
    echo "Build failed with exit code $exit_code"
fi

exit $exit_code
