@echo off
setlocal EnableExtensions DisableDelayedExpansion
if not "%~1"=="" (
    echo Usage: setup.bat
    exit /b 1
)
set "VOICE_SETUP_FILE=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$parts=(Get-Content -Raw -LiteralPath $env:VOICE_SETUP_FILE) -split '(?m)^# POWERSHELL-BEGIN\r?$',2; & ([scriptblock]::Create($parts[1]))"
exit /b %errorlevel%

# POWERSHELL-BEGIN
# PowerShell is embedded here so setup needs only this one Windows file.
$ErrorActionPreference = 'Stop'

function Invoke-Checked($program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -eq 3010) { throw 'Installation requires a reboot. Restart Windows and rerun setup.' }
    if ($LASTEXITCODE -ne 0) {
        throw "$program failed (exit $LASTEXITCODE)."
    }
}

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
        [Environment]::GetEnvironmentVariable('Path', 'User') + ';' + $env:Path
}

function Install-WingetPackage($id, [string[]]$extra = @()) {
    if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        Write-Host 'Installing Windows Package Manager for the current user...'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Install-PackageProvider -Name NuGet -Scope CurrentUser -Force | Out-Null
        Install-Module Microsoft.WinGet.Client -Scope CurrentUser -Repository PSGallery -Force
        Import-Module Microsoft.WinGet.Client
        Repair-WinGetPackageManager
        Refresh-Path
    }
    Invoke-Checked winget.exe (@('install', '--id', $id, '--exact', '--source', 'winget',
        '--accept-package-agreements', '--accept-source-agreements') + $extra)
    Refresh-Path
}

function Ensure-Tool($command, $package) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        Install-WingetPackage $package
    }
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command is unavailable after installation. Restart your terminal and rerun setup."
    }
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
}

function Import-NativeTools($installation) {
    $devcmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $env:ComSpec
    $start.Arguments = '/d /s /c "call "' + $devcmd + '" -arch=x64 -host_arch=x64 >nul && set"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $process = [Diagnostics.Process]::Start($start)
    $output = $process.StandardOutput.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw 'Visual Studio x64 environment initialization failed.' }
    foreach ($line in ($output -split '\r?\n')) {
        if ($line -match '^(PATH|INCLUDE|LIB|LIBPATH|VSCMD_ARG_TGT_ARCH)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}

function Read-Json($path, $fallback) {
    if (-not (Test-Path -LiteralPath $path)) { return $fallback }
    $text = [IO.File]::ReadAllText($path)
    # Keep quoted strings intact while accepting VS Code comments/trailing commas.
    $text = [regex]::Replace($text, '"(?:\\.|[^"\\])*"|//[^\r\n]*|/\*[\s\S]*?\*/', {
        param($match)
        if ($match.Value.StartsWith('"')) { $match.Value }
    })
    $text = [regex]::Replace($text, '"(?:\\.|[^"\\])*"|,(?=\s*[}\]])', {
        param($match)
        if ($match.Value -ne ',') { $match.Value }
    })
    $result = ConvertFrom-Json -InputObject $text
    if ($null -eq $result -or $result -is [array] -or $result -isnot [pscustomobject]) {
        throw "$path must contain a JSON object."
    }
    return $result
}

function Set-Field($object, $name, $value) {
    $object | Add-Member -MemberType NoteProperty -Name $name -Value $value -Force
}

function Write-Json($path, $object) {
    $text = ($object | ConvertTo-Json -Depth 30) + [Environment]::NewLine
    if (Test-Path -LiteralPath $path) {
        if ([IO.File]::ReadAllText($path) -eq $text) { return }
        if (-not (Test-Path -LiteralPath "$path.setup-backup")) {
            Copy-Item -LiteralPath $path -Destination "$path.setup-backup"
        }
    }
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false))
}

try {
    $project = [IO.Path]::GetFullPath((Join-Path (Split-Path $env:VOICE_SETUP_FILE) '..'))
    $editor = Join-Path $project '.vscode'
    # Validate existing editor files before installing anything.
    $settings = Read-Json (Join-Path $editor 'settings.json') ([pscustomobject]@{})
    $properties = Read-Json (Join-Path $editor 'c_cpp_properties.json') ([pscustomobject]@{configurations=@(); version=4})
    $extensions = Read-Json (Join-Path $editor 'extensions.json') ([pscustomobject]@{recommendations=@()})
    $tasks = Read-Json (Join-Path $editor 'tasks.json') ([pscustomobject]@{version='2.0.0'; tasks=@()})

    Refresh-Path
    Ensure-Tool git.exe Git.Git
    Ensure-Tool cmake.exe Kitware.CMake
    Ensure-Tool ninja.exe Ninja-build.Ninja
    $cmakeVersion = (& cmake --version | Select-Object -First 1) -replace '^cmake version ', ''
    if ([version]$cmakeVersion -lt [version]'3.24') {
        Install-WingetPackage Kitware.CMake @('--force')
        $cmakeVersion = (& cmake --version | Select-Object -First 1) -replace '^cmake version ', ''
        if ([version]$cmakeVersion -lt [version]'3.24') { throw 'CMake 3.24 or newer is required on Windows.' }
    }

    $visualStudio = Find-VisualStudio
    if (-not $visualStudio) {
        Write-Host 'Installing Visual Studio C++ Build Tools and Windows SDK...'
        Install-WingetPackage Microsoft.VisualStudio.2022.BuildTools @('--force', '--override',
            '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
        $visualStudio = Find-VisualStudio
    }
    if (-not $visualStudio) { throw 'Visual Studio C++ tools unavailable. Restart after installation and rerun setup.' }
    Import-NativeTools $visualStudio
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'x64 MSVC compiler unavailable.' }
    if (-not (@($env:INCLUDE -split ';' | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Windows.h')) }).Count)) {
        Write-Host 'Adding the recommended Windows SDK to the existing C++ installation...'
        $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
        $vswhere = Join-Path (Split-Path $installer) 'vswhere.exe'
        $instance = @(& $vswhere -products '*' -format json | ConvertFrom-Json) | Where-Object installationPath -eq $visualStudio | Select-Object -First 1
        $arguments = 'modify --installPath "' + $visualStudio + '" --channelId "' + $instance.channelId +
            '" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --norestart'
        $update = Start-Process -FilePath $installer -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if ($update.ExitCode -eq 3010) { throw 'SDK installation requires a reboot. Restart Windows and rerun setup.' }
        if ($update.ExitCode -ne 0) { throw "Windows SDK installation failed (exit $($update.ExitCode))." }
        Import-NativeTools $visualStudio
        if (-not (@($env:INCLUDE -split ';' | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Windows.h')) }).Count)) {
            throw 'Windows SDK unavailable after installation. Restart your terminal and rerun setup.'
        }
    }

    if (-not $env:VCPKG_ROOT) {
        $env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
    }
    $cmakeExecutable = (Get-Command cmake.exe).Source
    $cache = Join-Path $project 'build\CMakeCache.txt'
    if (-not $env:VCPKG_ROOT -and (Test-Path -LiteralPath $cache)) {
        $cachedToolchain = Select-String -LiteralPath $cache -Pattern '^CMAKE_TOOLCHAIN_FILE:[^=]+=(.+[/\\]scripts[/\\]buildsystems[/\\]vcpkg\.cmake)$'
        if ($cachedToolchain) {
            $previousRoot = Split-Path (Split-Path (Split-Path $cachedToolchain.Matches[0].Groups[1].Value))
            if (Test-Path -LiteralPath (Join-Path $previousRoot 'bootstrap-vcpkg.bat')) { $env:VCPKG_ROOT = $previousRoot }
        }
    }
    if (-not $env:VCPKG_ROOT) {
        $env:VCPKG_ROOT = Join-Path $env:LOCALAPPDATA 'realtime-voice\vcpkg'
    }
    $env:VCPKG_ROOT = [IO.Path]::GetFullPath($env:VCPKG_ROOT)
    if (-not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'bootstrap-vcpkg.bat'))) {
        if (Test-Path -LiteralPath $env:VCPKG_ROOT) { throw "Not a vcpkg checkout: $env:VCPKG_ROOT" }
        New-Item -ItemType Directory -Force -Path (Split-Path $env:VCPKG_ROOT) | Out-Null
        Invoke-Checked git.exe @('clone', 'https://github.com/microsoft/vcpkg.git', $env:VCPKG_ROOT)
    }
    $vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkg)) {
        Invoke-Checked (Join-Path $env:VCPKG_ROOT 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    $installed = Join-Path $env:VCPKG_ROOT 'installed\x64-windows'
    $toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'

    # Reuse Boost from an existing build, explicit environment, or Native Tools.
    $boost = $env:BOOST_INCLUDEDIR
    if (-not $boost -and (Test-Path -LiteralPath $cache)) {
        $entry = Select-String -LiteralPath $cache -Pattern '^BOOST_INCLUDE_DIR:PATH=(.+)$'
        if ($entry) { $boost = $entry.Matches[0].Groups[1].Value }
    }
    if (-not $boost -or -not (Test-Path -LiteralPath (Join-Path $boost 'boost\asio.hpp'))) {
        $boost = $null
        $candidates = @((Join-Path $installed 'include'), $env:BOOST_ROOT) + @($env:INCLUDE -split ';')
        foreach ($candidate in $candidates) {
            if (-not $candidate) { continue }
            $directories = @($candidate, (Join-Path $candidate 'include')) +
                @(Get-ChildItem -LiteralPath $candidate -Directory -Filter 'boost-*' -ErrorAction SilentlyContinue | ForEach-Object FullName)
            foreach ($directory in $directories) {
                if (Test-Path -LiteralPath (Join-Path $directory 'boost\asio.hpp')) { $boost = $directory; break }
            }
            if ($boost) { break }
        }
    }
    $missing = @()
    if (-not $boost) { $missing += 'boost-asio:x64-windows' }
    foreach ($file in @('share\opus\OpusConfig.cmake', 'include\opus\opus.h', 'lib\opus.lib', 'bin\opus.dll', 'debug\lib\opus.lib', 'debug\bin\opus.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installed $file))) { $missing += 'opus:x64-windows'; break }
    }
    if ($missing.Count) { Invoke-Checked $vcpkg (@('install') + $missing) }
    if (-not $boost) { $boost = Join-Path $installed 'include' }
    $env:BOOST_INCLUDEDIR = $boost

    $configure = @('-S', $project, '-B', (Join-Path $project 'build'), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_CXX_COMPILER=cl', "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DVCPKG_INSTALLED_DIR=$(Join-Path $env:VCPKG_ROOT 'installed')", '-DVCPKG_TARGET_TRIPLET=x64-windows')
    if ((Test-Path -LiteralPath $cache) -and -not (Select-String -LiteralPath $cache -Pattern '^VCPKG_INSTALLED_DIR:')) {
        $configure = @('--fresh') + $configure
    }
    Invoke-Checked $cmakeExecutable $configure
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $env:VCPKG_ROOT, 'User')
    [Environment]::SetEnvironmentVariable('BOOST_INCLUDEDIR', $boost, 'User')

    New-Item -ItemType Directory -Force -Path $editor | Out-Null
    $includes = @('${workspaceFolder}/include', $boost.Replace('\', '/'),
        (Join-Path $installed 'include').Replace('\', '/'), (Join-Path $installed 'include\opus').Replace('\', '/')) | Select-Object -Unique
    $configuration = [pscustomobject]@{name='CMake'; compilerPath=(Get-Command cl.exe).Source.Replace('\', '/');
        cStandard='c17'; cppStandard='c++17'; intelliSenseMode='windows-msvc-x64';
        configurationProvider='ms-vscode.cmake-tools'; compileCommands='${workspaceFolder}/build/compile_commands.json';
        includePath=@($includes); defines=@('VOICE_DEBUG')}
    Set-Field $properties 'configurations' (@($configuration) + @($properties.configurations | Where-Object name -ne 'CMake'))
    Set-Field $properties 'version' 4
    Set-Field $settings 'C_Cpp.default.configurationProvider' 'ms-vscode.cmake-tools'
    Set-Field $settings 'C_Cpp.default.compileCommands' '${workspaceFolder}/build/compile_commands.json'
    Set-Field $settings 'cmake.configureOnOpen' $true
    Set-Field $settings 'cmake.buildDirectory' '${workspaceFolder}/build'
    Set-Field $settings 'cmake.generator' 'Ninja'
    Set-Field $settings 'cmake.cmakePath' $cmakeExecutable.Replace('\', '/')
    $configureSettings = $settings.'cmake.configureSettings'
    if (-not $configureSettings) { $configureSettings = [pscustomobject]@{} }
    Set-Field $configureSettings 'CMAKE_BUILD_TYPE' 'Debug'
    Set-Field $configureSettings 'CMAKE_CXX_COMPILER' (Get-Command cl.exe).Source.Replace('\', '/')
    Set-Field $configureSettings 'CMAKE_TOOLCHAIN_FILE' $toolchain.Replace('\', '/')
    Set-Field $configureSettings 'VCPKG_INSTALLED_DIR' (Join-Path $env:VCPKG_ROOT 'installed').Replace('\', '/')
    Set-Field $configureSettings 'VCPKG_TARGET_TRIPLET' 'x64-windows'
    Set-Field $configureSettings 'BOOST_INCLUDE_DIR' $boost.Replace('\', '/')
    Set-Field $settings 'cmake.configureSettings' $configureSettings
    # VS Code can be opened from Explorer without a Native Tools environment.
    foreach ($key in @('cmake.configureEnvironment', 'cmake.buildEnvironment')) {
        $environment = $settings.$key
        if (-not $environment) { $environment = [pscustomobject]@{} }
        foreach ($name in @('INCLUDE', 'LIB', 'LIBPATH')) {
            Set-Field $environment $name ([Environment]::GetEnvironmentVariable($name, 'Process'))
        }
        Set-Field $environment 'PATH' ($env:Path + ';${env:PATH}')
        Set-Field $settings $key $environment
    }
    Set-Field $extensions 'recommendations' @(@($extensions.recommendations) + @('ms-vscode.cpptools', 'ms-vscode.cmake-tools') | Select-Object -Unique)
    $buildTasks = foreach ($mode in @('Debug', 'Release')) {
        [pscustomobject]@{label="voice: build $mode"; type='shell'; command='.\build.bat'; args=@("-$($mode.ToLower())");
            options=@{cwd='${workspaceFolder}'; shell=@{executable='cmd.exe'; args=@('/d', '/c')}};
            problemMatcher=@('$msCompile'); group=@{kind='build'; isDefault=($mode -eq 'Debug')}}
    }
    Set-Field $tasks 'tasks' (@($tasks.tasks | Where-Object { $_.label -notin @('voice: build Debug', 'voice: build Release') }) + @($buildTasks))
    Set-Field $tasks 'version' '2.0.0'
    Write-Json (Join-Path $editor 'settings.json') $settings
    Write-Json (Join-Path $editor 'c_cpp_properties.json') $properties
    Write-Json (Join-Path $editor 'extensions.json') $extensions
    Write-Json (Join-Path $editor 'tasks.json') $tasks
    Write-Host 'Setup complete. Open this folder in VS Code and install its recommended extensions.'
    Write-Host 'Build with build.bat (Debug) or build.bat -release. Restart existing terminals to refresh environment variables.'
    exit 0
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
