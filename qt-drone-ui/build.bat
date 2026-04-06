@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Simple build script for Qt Drone UI
echo Building Qt Drone UI...

REM Always run from repo root regardless of caller location
pushd "%~dp0"

REM Set Qt environment
set "PATH=C:\Qt\6.10.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
set "CC=C:\Qt\Tools\mingw1310_64\bin\gcc.exe"
set "CXX=C:\Qt\Tools\mingw1310_64\bin\g++.exe"
set "GENERATOR=Ninja"

REM Configure and build
if not exist build mkdir build

if exist build\CMakeCache.txt (
    set "CACHE_GENERATOR="
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" build\CMakeCache.txt') do set "CACHE_GENERATOR=%%G"
    if defined CACHE_GENERATOR (
        if /I not "!CACHE_GENERATOR!"=="%GENERATOR%" (
            echo Generator changed from "!CACHE_GENERATOR!" to "%GENERATOR%". Recreating build folder...
            rmdir /s /q build 2>nul
            if exist build (
                echo Failed to recreate build folder. Close any process using files in "build" and try again.
                goto :fail
            )
            mkdir build >nul 2>&1
            if errorlevel 1 (
                echo Failed to create build folder.
                goto :fail
            )
        )
    )
)

cmake -S . -B build -G "%GENERATOR%"
if errorlevel 1 goto :fail

cmake --build build --parallel --verbose
if errorlevel 1 goto :fail

echo Build successful!
echo Executable: build\QtDroneUI.exe
popd
exit /b 0

:fail
echo Build failed!
if exist build\QtDroneUI.exe (
    echo If linking failed with "Access is denied", close any running QtDroneUI instance and retry.
)
popd
exit /b 1
