
@echo off
setlocal EnableExtensions DisableDelayedExpansion

REM Project paths
REM Normalize the directory so a trailing backslash cannot escape a closing quote.
for %%I in ("%~dp0.") do set "PROJECT_DIR=%%~fI"
set "BUILD_DIR=%PROJECT_DIR%\build"

REM Validate every argument before cleaning or configuring anything.
set "BUILD_TYPE=Debug"
set "EXPLICIT_MODE="
set "CLEAN_BUILD="
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="-clean" (
    set "CLEAN_BUILD=1"
) else if /I "%~1"=="-debug" (
    if /I "%EXPLICIT_MODE%"=="Release" goto conflicting_modes
    set "EXPLICIT_MODE=Debug"
    set "BUILD_TYPE=Debug"
) else if /I "%~1"=="-release" (
    if /I "%EXPLICIT_MODE%"=="Debug" goto conflicting_modes
    set "EXPLICIT_MODE=Release"
    set "BUILD_TYPE=Release"
) else (
    goto usage
)
shift
goto parse_args
:args_done

REM Clean only when explicitly requested
if defined CLEAN_BUILD (
    echo Cleaning previous build...

    if exist "%BUILD_DIR%" (
        powershell -NoProfile -Command "$target=[IO.Path]::GetFullPath($env:BUILD_DIR); $expected=[IO.Path]::Combine([IO.Path]::GetFullPath($env:PROJECT_DIR),'build'); if($target -ne $expected -or (Get-Item -LiteralPath $target -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)){throw 'Unsafe build directory'}; for($attempt=0;$attempt -lt 4;$attempt++){try{if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction Stop}; break}catch{if($attempt -eq 3){throw}; Start-Sleep -Milliseconds 500}}"
        if errorlevel 1 goto failed
    )
)

REM Check vcpkg environment
REM A setup run in another terminal may have persisted this at user scope.
if not defined VCPKG_ROOT (
    for /F "tokens=2,*" %%A in ('reg query HKCU\Environment /v VCPKG_ROOT 2^>nul') do set "VCPKG_ROOT=%%B"
)
if not defined VCPKG_ROOT (
    echo ERROR: VCPKG_ROOT is not set.
    echo Set VCPKG_ROOT to your vcpkg installation directory.
    exit /b 1
)
if not defined BOOST_INCLUDEDIR (
    for /F "tokens=2,*" %%A in ('reg query HKCU\Environment /v BOOST_INCLUDEDIR 2^>nul') do set "BOOST_INCLUDEDIR=%%B"
)

REM Always use the x64 MSVC environment, even from an ordinary CMD prompt.
REM VsDevCmd can replace VCPKG_ROOT with Visual Studio's bundled checkout.
set "VOICE_VCPKG_ROOT=%VCPKG_ROOT%"
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    call :native_tools
    if errorlevel 1 goto failed
)
set "VCPKG_ROOT=%VOICE_VCPKG_ROOT%"

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
echo Configuring Windows %BUILD_TYPE% build...

cmake %CONFIGURE_REFRESH% -S "%PROJECT_DIR%" -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_CXX_COMPILER=cl ^
    "-DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN%" ^
    "-DVCPKG_INSTALLED_DIR=%VCPKG_ROOT%\installed" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows

if not "%errorlevel%"=="0" goto failed

REM Build
echo Building project...

cmake --build "%BUILD_DIR%" --parallel

if not "%errorlevel%"=="0" goto failed

echo.
echo Build successful!
echo Output: %BUILD_DIR%
exit /b 0

:usage
echo Usage: build.bat [-debug ^| -release] [-clean]
echo Default: Debug. Flags may be entered in any order.
exit /b 1

:conflicting_modes
echo ERROR: -debug and -release cannot be used together.
goto usage

:native_tools
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio C++ tools not found. Run setup\setup.bat first.
    exit /b 1
)
set "VS_DIR="
for /F "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_DIR=%%V"
if not defined VS_DIR (
    echo ERROR: Install Visual Studio's Desktop development with C++ workload.
    exit /b 1
)
call "%VS_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
exit /b %errorlevel%

:failed
echo.
echo ERROR: Build failed.
exit /b 1
