@echo off
setlocal
cd /d "%~dp0"
if not exist "build\cli\fortressfinder-cli.exe" (
    echo Build first: powershell -ExecutionPolicy Bypass -File build_cli_msys2.ps1
    exit /b 1
)
"build\cli\fortressfinder-cli.exe" %*
