
@echo off
setlocal

REM Project paths
REM Normalize the directory so a trailing backslash cannot escape a closing quote.
for %%I in ("%~dp0.") do set "PROJECT_DIR=%%~fI"
set "BUILD_DIR=%PROJECT_DIR%\build"

REM Validate arguments
if not "%~1"=="" if /I not "%~1"=="-clean" goto usage
if not "%~2"=="" goto usage

REM Clean only when explicitly requested
if /I "%~1"=="-clean" (
    echo Cleaning previous build...

    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
        if errorlevel 1 goto failed
    )
)

REM Check vcpkg environment
if not defined VCPKG_ROOT (
    echo ERROR: VCPKG_ROOT is not set.
    echo Set VCPKG_ROOT to your vcpkg installation directory.
    exit /b 1
)

set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

if not exist "%TOOLCHAIN%" (
    echo ERROR: vcpkg toolchain not found:
    echo %TOOLCHAIN%
    exit /b 1
)

REM Preserve an existing Boost location when refreshing the CMake cache.
if not defined BOOST_INCLUDEDIR if exist "%BUILD_DIR%\CMakeCache.txt" (
    for /F "tokens=1,* delims==" %%A in ('findstr /B /C:"BOOST_INCLUDE_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt"') do (
        if exist "%%B\boost\asio.hpp" set "BOOST_INCLUDEDIR=%%B"
    )
)

REM Discover Boost from the Native Tools include paths, including versioned installs.
if not defined BOOST_INCLUDEDIR (
    for %%B in ("%INCLUDE:;=" "%") do (
        if exist "%%~B\boost\asio.hpp" set "BOOST_INCLUDEDIR=%%~B"
        for /D %%V in ("%%~B\boost-*") do (
            if exist "%%V\boost\asio.hpp" set "BOOST_INCLUDEDIR=%%V"
        )
    )
)

REM A cache created without vcpkg will not load a toolchain added later.
REM Refresh that configuration once; normal builds remain incremental.
set "CONFIGURE_REFRESH="
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /B /C:"VCPKG_INSTALLED_DIR:" "%BUILD_DIR%\CMakeCache.txt" >nul
    if errorlevel 1 (
        echo Refreshing CMake cache to enable vcpkg integration...
        set "CONFIGURE_REFRESH=--fresh"
    )
)

REM Configure
echo Configuring Windows Release build...

cmake %CONFIGURE_REFRESH% -S "%PROJECT_DIR%" -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN%" ^
    "-DVCPKG_INSTALLED_DIR=%VCPKG_ROOT%\installed" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows

if errorlevel 1 goto failed

REM Build
echo Building project...

cmake --build "%BUILD_DIR%" --parallel

if errorlevel 1 goto failed

echo.
echo Build successful!
echo Output: %BUILD_DIR%
exit /b 0

:usage
echo Usage: build.bat [-clean]
exit /b 1

:failed
echo.
echo ERROR: Build failed.
exit /b 1
