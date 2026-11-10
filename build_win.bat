@echo off
setlocal enabledelayedexpansion
title Building OpenGC (Windows x86)

echo ===================================================
echo           OpenGC Windows Build Script
echo ===================================================
echo.

cd /d "%~dp0"

:: 1. Locate Visual Studio / BuildTools using vswhere or standard locations
set "VS_DEV_CMD="

if defined VS_BUILDTOOLS (
    if exist "%VS_BUILDTOOLS%\Common7\Tools\VsDevCmd.bat" (
        set "VS_DEV_CMD=%VS_BUILDTOOLS%\Common7\Tools\VsDevCmd.bat"
        goto :FoundVS
    )
)

set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE_EXE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE_EXE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\Common7\Tools\VsDevCmd.bat" (
            set "VS_DEV_CMD=%%i\Common7\Tools\VsDevCmd.bat"
            goto :FoundVS
        )
    )
)

if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    goto :FoundVS
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    goto :FoundVS
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    goto :FoundVS
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    goto :FoundVS
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    goto :FoundVS
)

echo [!] ERROR: Visual Studio Developer Command Prompt not found.
echo Please install Visual Studio 2022 with C++ Desktop Development or set VS_BUILDTOOLS.
pause
exit /b 1

:FoundVS
echo [*] Visual Studio found: "%VS_DEV_CMD%"
echo [*] Initializing MSVC x86 Developer Environment...

call "%VS_DEV_CMD%" -arch=x86 -host_arch=x64
if errorlevel 1 (
    echo [!] ERROR: Failed to initialize x86 developer environment.
    pause
    exit /b 1
)

:: 2. Locate or Setup vcpkg
if not defined VCPKG_ROOT (
    if exist "%~dp0vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=%~dp0vcpkg"
    ) else if exist "C:\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=C:\vcpkg"
    ) else if exist "D:\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=D:\vcpkg"
    )
)

if not defined VCPKG_ROOT (
    echo [*] vcpkg not detected. Cloning vcpkg locally into "%~dp0vcpkg"...
    git clone https://github.com/microsoft/vcpkg.git "%~dp0vcpkg"
    if errorlevel 1 (
        echo [!] ERROR: Git clone vcpkg failed. Please set VCPKG_ROOT manually.
        pause
        exit /b 1
    )
    set "VCPKG_ROOT=%~dp0vcpkg"
    echo [*] Bootstrapping vcpkg...
    call "!VCPKG_ROOT!\bootstrap-vcpkg.bat" -disableMetrics
)

echo [*] Using vcpkg: "%VCPKG_ROOT%"

:: 3. Determine Generator (Ninja preferred, otherwise Visual Studio)
set "CMAKE_EXE=cmake"
where ninja >nul 2>&1
if %errorlevel% equ 0 (
    set CMAKE_GEN=-G Ninja
    echo [*] Generator: Ninja
) else (
    set CMAKE_GEN=-G "Visual Studio 17 2022" -A Win32
    echo [*] Generator: Visual Studio 17 2022 (Win32)
)

if not exist "%~dp0release" mkdir "%~dp0release"

echo.
echo [*] Configuring CMake...
%CMAKE_EXE% -S "%~dp0." -B "%~dp0build" %CMAKE_GEN% -DCMAKE_BUILD_TYPE=Release -DOUTDIR="%~dp0release" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static

if errorlevel 1 (
    echo [!] CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo [*] Compiling targets: csgo_gc, csgo, srcds...
%CMAKE_EXE% --build "%~dp0build" --config Release --target csgo_gc csgo srcds

if errorlevel 1 (
    echo [!] Build failed!
    pause
    exit /b 1
)

echo.
echo ===================================================
echo [OK] OpenGC build completed successfully!
echo Artifacts placed in build/ and release/
echo ===================================================
echo.
pause
