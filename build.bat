@echo off
REM ============================================================================
REM  Bangla Voice Typing Keyboard — One-Click Build Script
REM  Requirements: Visual Studio 2022 (or Build Tools for VS 2022)
REM ============================================================================

echo.
echo ============================================================
echo   Bangla Voice Typing Keyboard — Build Script
echo ============================================================
echo.

REM --- Find Visual Studio installation ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio 2022 not found.
    echo         Install "Build Tools for Visual Studio 2022" from:
    echo         https://visualstudio.microsoft.com/downloads/
    echo.
    pause
    exit /b 1
)

REM --- Get VS installation path ---
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo [ERROR] Visual Studio C++ tools not found.
    echo         Install the "Desktop development with C++" workload.
    echo.
    pause
    exit /b 1
)

echo [OK] Found Visual Studio at: %VS_PATH%

REM --- Set up MSVC environment ---
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

if errorlevel 1 (
    echo [ERROR] Failed to initialize MSVC environment.
    pause
    exit /b 1
)

echo [OK] MSVC environment initialized (x64)

REM --- Create build directory ---
if not exist "build" mkdir build
cd build

REM --- Run CMake ---
echo.
echo [BUILD] Running CMake...
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    cd ..
    pause
    exit /b 1
)

echo [OK] CMake configuration complete

REM --- Build ---
echo.
echo [BUILD] Compiling...
cmake --build . --config Release

if errorlevel 1 (
    echo [ERROR] Build failed.
    cd ..
    pause
    exit /b 1
)

cd ..

REM --- Success ---
echo.
echo ============================================================
echo   BUILD SUCCESSFUL!
echo ============================================================
echo.
echo   Output: bin\Release\BanglaVoiceTyping.exe
echo.
echo   To run:
echo     1. Make sure config.txt is in the same folder as the .exe
echo     2. Double-click BanglaVoiceTyping.exe
echo     3. Press Alt+X to start/stop recording
echo.
pause
