@echo off
REM Simple launcher for Qt Drone UI
echo Starting Qt Drone UI...

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
    exit /b 1
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
    exit /b 1
)

REM Set Qt environment
set "PATH=%QT_VERSION_DIR%\mingw_64\bin;%MINGW_DIR%\bin;%PATH%"

echo Using Qt: %QT_VERSION_DIR%
echo Using MinGW: %MINGW_DIR%

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