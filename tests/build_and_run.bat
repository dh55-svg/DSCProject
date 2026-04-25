@echo off
setlocal enabledelayedexpansion

REM Build and run all unit tests for MYDSCProject
REM Requires: CMake, Visual Studio 2019 (v142), Qt 6.5.3

set QT_DIR=C:\Qt\6.5.3\msvc2019_64
set BUILD_DIR=build

if not exist "%QT_DIR%" (
    echo ERROR: Qt not found at %QT_DIR%
    echo Please set QT_DIR to your Qt 6.5.3 MSVC 2019 installation
    exit /b 1
)

REM Find VS 2019 (v142)
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

for /f "usebackq tokens=*" %%i in (`%VSWHERE% -version 16 -property installationPath`) do (
    set VS_DIR=%%i
)

if not defined VS_DIR (
    echo ERROR: Visual Studio 2019 not found
    exit /b 1
)

call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

echo === Configuring tests with CMake ===
cmake -B %BUILD_DIR% -S . -DCMAKE_PREFIX_PATH=%QT_DIR% -DCMAKE_BUILD_TYPE=Debug
if %ERRORLEVEL% neq 0 (
    echo CMake configuration FAILED
    exit /b %ERRORLEVEL%
)

echo === Building tests ===
cmake --build %BUILD_DIR% --config Debug
if %ERRORLEVEL% neq 0 (
    echo Build FAILED
    exit /b %ERRORLEVEL%
)

echo.
echo === Running tests via CTest ===
cd %BUILD_DIR%
ctest --output-on-failure -C Debug
cd ..

echo.
echo === Running tests individually ===
for %%t in (Debug\TestDoubleBuffer.exe Debug\TestAlarmEngine.exe Debug\TestAuthManager.exe) do (
    if exist %BUILD_DIR%\%%t (
        echo.
        echo ---- %%t ----
        %BUILD_DIR%\%%t
    )
)

echo.
echo === All tests completed ===
