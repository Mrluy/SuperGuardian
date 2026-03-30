@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

echo ============================================
echo  SuperGuardian - Static Qt 6.10.2 Build
echo ============================================
echo.

REM === Configuration ===
set QT_VERSION=6.10.2
set QT_STATIC_PREFIX=C:\Qt\%QT_VERSION%\msvc2022_64_static
set QT_SRC_DIR=C:\Qt\%QT_VERSION%\Src\qtbase
set QT_BUILD_DIR=C:\Qt\%QT_VERSION%\static_build

REM === Find Visual Studio ===
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "tokens=*" %%i in ('%VSWHERE% -latest -property installationPath') do set VS_PATH=%%i

if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [ERROR] Cannot find vcvarsall.bat
    exit /b 1
)

echo [1/6] Setting up MSVC x64 environment...
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [ERROR] Failed to set up MSVC environment
    exit /b 1
)

REM === Check source ===
if not exist "%QT_SRC_DIR%\configure.bat" (
    echo [2/6] Cloning Qt %QT_VERSION% qtbase source...
    git clone --depth 1 --branch v%QT_VERSION% https://code.qt.io/qt/qtbase.git "%QT_SRC_DIR%"
    if errorlevel 1 (
        echo [ERROR] Git clone failed
        exit /b 1
    )
) else (
    echo [2/6] Source already exists: %QT_SRC_DIR%
)

REM === Clean previous build ===
echo [3/6] Preparing build directory...
if exist "%QT_BUILD_DIR%" rmdir /S /Q "%QT_BUILD_DIR%"
mkdir "%QT_BUILD_DIR%"
cd /d "%QT_BUILD_DIR%"

REM === Configure static build ===
echo [4/6] Configuring static Qt build...
call "%QT_SRC_DIR%\configure.bat" ^
    -static ^
    -static-runtime ^
    -release ^
    -optimize-size ^
    -prefix "%QT_STATIC_PREFIX%" ^
    -nomake examples ^
    -nomake tests ^
    -no-opengl ^
    -no-feature-sql ^
    -no-feature-network ^
    -no-feature-dbus ^
    -no-feature-testlib ^
    -no-feature-printsupport ^
    -no-feature-concurrent
if errorlevel 1 (
    echo [ERROR] Configure failed
    exit /b 1
)

REM === Build ===
echo [5/6] Building static Qt (this takes 15-30 minutes)...
cmake --build . --parallel
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

REM === Install ===
echo [6/6] Installing to %QT_STATIC_PREFIX%...
cmake --install .
if errorlevel 1 (
    echo [ERROR] Install failed
    exit /b 1
)

REM === Register in Qt VS Tools ===
echo Registering static Qt in Visual Studio...
reg add "HKCU\Software\QtProject\QtVsTools\Versions\Qt6.10.2_x64_static" /v InstallDir /d "%QT_STATIC_PREFIX%" /f

echo.
echo ============================================
echo  Static Qt build complete!
echo  Install: %QT_STATIC_PREFIX%
echo  VS Name: Qt6.10.2_x64_static
echo ============================================
pause
