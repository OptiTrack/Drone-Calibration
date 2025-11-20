@echo off
REM Simple build script for Qt Drone UI
echo Building Qt Drone UI...

REM Set Qt environment
set PATH=C:\Qt\6.10.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%
set CC=C:\Qt\Tools\mingw1310_64\bin\gcc.exe
set CXX=C:\Qt\Tools\mingw1310_64\bin\g++.exe

REM Create build directory
if not exist build mkdir build
cd build

REM Configure and build
cmake .. -G Ninja
if %ERRORLEVEL% NEQ 0 (
    echo Configuration failed!
    pause
    exit /b 1
)

ninja
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo Build successful!
echo Executable: build\QtDroneUI.exe
pause