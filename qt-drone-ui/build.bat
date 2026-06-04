@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Simple build script for Qt Drone UI
echo Building Qt Drone UI...

REM Always run from repo root regardless of caller location
pushd "%~dp0"

REM Locate Qt and build tools. Set QT_VERSION before running to force a version.
set "QT_ROOT=C:\Qt"
set "QT_VERSION_DIR="
if defined QT_VERSION (
    if exist "%QT_ROOT%\%QT_VERSION%\mingw_64\bin\qmake.exe" (
        set "QT_VERSION_DIR=%QT_ROOT%\%QT_VERSION%"
    )
)
if not defined QT_VERSION_DIR (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%QT_ROOT%\6.*" 2^>nul') do (
        if exist "%QT_ROOT%\%%D\mingw_64\bin\qmake.exe" (
            set "QT_VERSION_DIR=%QT_ROOT%\%%D"
            goto :qt_found
        )
    )
)
:qt_found
if not defined QT_VERSION_DIR (
    echo Could not find a Qt 6 MinGW kit under "%QT_ROOT%".
    echo Install Qt with the MinGW component, or set QT_VERSION to an installed version.
    goto :fail
)

set "MINGW_DIR="
for /f "delims=" %%D in ('dir /b /ad /o-n "%QT_ROOT%\Tools\mingw*_64" 2^>nul') do (
    if exist "%QT_ROOT%\Tools\%%D\bin\gcc.exe" (
        set "MINGW_DIR=%QT_ROOT%\Tools\%%D"
        goto :mingw_found
    )
)
:mingw_found
if not defined MINGW_DIR (
    echo Could not find a MinGW toolchain under "%QT_ROOT%\Tools".
    goto :fail
)

REM Set Qt environment
set "PATH=%QT_VERSION_DIR%\mingw_64\bin;%MINGW_DIR%\bin;%QT_ROOT%\Tools\CMake_64\bin;%QT_ROOT%\Tools\Ninja;%PATH%"
set "CC=%MINGW_DIR%\bin\gcc.exe"
set "CXX=%MINGW_DIR%\bin\g++.exe"
set "GENERATOR=Ninja"

echo Using Qt: %QT_VERSION_DIR%
echo Using MinGW: %MINGW_DIR%

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
