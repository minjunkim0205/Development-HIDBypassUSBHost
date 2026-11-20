
@echo off
setlocal
title HID Mouse Bridge - Web Server

:: Move to the directory containing this batch file
cd /d "%~dp0"

:: Check Caddy executable
if not exist "caddy_windows_amd64.exe" (
    echo [ERROR] Caddy executable not found.
    pause
    exit /b 1
)

:: Check Caddyfile
if not exist "Caddyfile" (
    echo [ERROR] Caddyfile not found.
    pause
    exit /b 1
)

echo ==========================================
echo       HID Mouse Bridge Web Driver
echo ==========================================
echo.
echo Directory: %cd%
echo.
echo Web Driver:
echo http://localhost:9090
echo.
echo Test Page:
echo http://localhost:9090/index.html
echo.
echo Press Ctrl+C to stop the server.
echo ==========================================
echo.

:: Start Caddy
"%~dp0caddy_windows_amd64.exe" run --config "%~dp0Caddyfile" --adapter caddyfile

echo.
echo [INFO] Caddy stopped.
pause
endlocal
