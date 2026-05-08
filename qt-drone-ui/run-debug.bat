@echo off
REM Foreground launcher for debugging crashes. Keeps console output visible and writes a log.
setlocal

set PATH=C:\Qt\6.10.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%
set LOG_FILE=%~dp0qt-drone-ui-debug.log

cd /d "%~dp0build"
if not exist QtDroneUI.exe (
    echo QtDroneUI.exe not found. Building first...
    cd /d "%~dp0"
    call build.bat
    cd build
)

echo Starting QtDroneUI.exe in foreground...
echo Log: %LOG_FILE%
echo ===== QtDroneUI debug run %DATE% %TIME% ===== > "%LOG_FILE%"
QtDroneUI.exe >> "%LOG_FILE%" 2>&1
set EXIT_CODE=%ERRORLEVEL%
echo QtDroneUI exited with code %EXIT_CODE%
echo Exit code: %EXIT_CODE% >> "%LOG_FILE%"
exit /b %EXIT_CODE%
