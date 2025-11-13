@echo off
setlocal

REM ================================================================================
REM ====  USER CONFIGURATION  — edit these four lines to match your environment  ====
REM ================================================================================

REM 1) Root of your Qt/CMake project (where CMakeLists.txt lives):
set "PROJECT_ROOT=H:\path\to\sports-data-metric-client"

REM 2) Where you installed Qt (MSVC 2022 64-bit). Adjust version if needed:
set "QT_DIR=C:\Qt\6.9.0\msvc2022_64"

REM 3) The name of your final executable (as built by CMake). Usually matches your project name:
set "EXE_NAME=sports-data-metrics-client.exe"

REM 4) Path to the NatNet folder:
set "NATNET_SRC=%PROJECT_ROOT%\src\connection\natnet"

--------------------------------------------------------------------------------
REM === Derived paths (you generally don’t need to change these) ===

set "BUILD_DIR=%PROJECT_ROOT%\build"
set "RELEASE_DIR=%BUILD_DIR%\Release"

--------------------------------------------------------------------------------
REM === Begin pipeline ===

echo.
echo =========================
echo "1) Creating build folder"
echo =========================
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
) else (
    echo   [OK] "%BUILD_DIR%" already exists.
)
cd /d "%BUILD_DIR%"

echo.
echo =========================================
echo "2) Configuring CMake (MSVC 2022 x64 + Qt)"
echo =========================================
cmake -G "Visual Studio 17 2022" ^
      -A x64 ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%\lib\cmake" ^
      "%PROJECT_ROOT%"
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

echo.
echo ==================================
echo "3) Building in Release configuration"
echo ==================================
cmake --build . --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo =================================================
echo "4) Running windeployqt to bundle Qt DLLs & plugins"
echo =================================================
cd /d "%RELEASE_DIR%"
pwd
"%QT_DIR%\bin\windeployqt.exe" --release --dir . "%EXE_NAME%"
if errorlevel 1 (
    echo ERROR: windeployqt failed.
    exit /b 1
)

echo.
echo ===========================================
echo "5) Copying NatNet files into Release directory"
echo ===========================================
robocopy "%NATNET_SRC%" "%RELEASE_DIR%" /E
if errorlevel 1 (
    REM Note: robocopy returns codes ≥ 8 on failure, 0–7 on success.
    echo WARNING: robocopy finished with exit code %ERRORLEVEL%.  Check for missing files.
)

echo.
echo =================================
echo   Release pipeline completed.
echo Release executable is in:
echo   %RELEASE_DIR%\%EXE_NAME%
echo Include everything in "%RELEASE_DIR%" when distributing.
echo =================================

endlocal