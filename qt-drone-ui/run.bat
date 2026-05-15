@echo off
REM Simple launcher for Qt Drone UI
echo Starting Qt Drone UI...

REM Set Qt environment
set PATH=C:\Qt\6.10.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%

REM Change to build directory and run
cd /d "%~dp0build"
if not exist QtDroneUI.exe (
    echo QtDroneUI.exe not found. Building first...
    cd /d "%~dp0"
    call build.bat
    cd build
)

REM Launch the application
start "" QtDroneUI.exe
echo Qt Drone UI launched!