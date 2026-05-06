param(
    [ValidateSet('Build', 'Rebuild', 'Clean')]
    [string]$Target = 'Build',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$solution = Join-Path $root 'Shaiya-Core.sln'
$msbuild = $null

if ($env:MSBUILD_EXE -and (Test-Path $env:MSBUILD_EXE)) {
    $msbuild = $env:MSBUILD_EXE
}

$candidate = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
if (-not $msbuild -and $candidate) {
    $msbuild = $candidate.Source
}

if (-not $msbuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
            Select-Object -First 1
    }
}

if (-not $msbuild) {
    $candidates = @(
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )

    $msbuild = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $msbuild) {
    throw 'MSBuild.exe was not found. Install Visual Studio 2022 Build Tools with the C++ workload.'
}

$msbuildTarget = 'sdev-client'
if ($Target -ne 'Build') {
    $msbuildTarget = "sdev-client:$Target"
}

& $msbuild $solution /t:$msbuildTarget /p:Configuration=$Configuration /p:Platform=x86 /m
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Target -ne 'Clean') {
    Get-Item (Join-Path $root "$Configuration\sdev-client.dll") |
        Select-Object FullName, Length, CreationTime, LastWriteTime
}
