@echo off
setlocal

set "ROOT=%~dp0"
set "MSBUILD="
set "TARGET=%~1"
set "CONFIGURATION=%~2"

if "%TARGET%"=="" set "TARGET=Build"
if "%CONFIGURATION%"=="" set "CONFIGURATION=Release"

if defined MSBUILD_EXE if exist "%MSBUILD_EXE%" set "MSBUILD=%MSBUILD_EXE%"

for %%I in (MSBuild.exe) do if not defined MSBUILD set "MSBUILD=%%~$PATH:I"

if not defined MSBUILD (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%I"
    )
)

if not defined MSBUILD (
    for %%I in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    ) do if exist "%%~I" if not defined MSBUILD set "MSBUILD=%%~I"
)

if "%MSBUILD%"=="" (
    echo MSBuild.exe was not found. Install Visual Studio 2022 Build Tools with the C++ workload.
    exit /b 1
)

set "MSBUILD_TARGET=sdev-client"
if /I not "%TARGET%"=="Build" set "MSBUILD_TARGET=sdev-client:%TARGET%"

"%MSBUILD%" "%ROOT%Shaiya-Core.sln" /t:%MSBUILD_TARGET% /p:Configuration=%CONFIGURATION% /p:Platform=x86 /m
if errorlevel 1 exit /b %errorlevel%

if /I not "%TARGET%"=="Clean" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Item '%ROOT%%CONFIGURATION%\sdev-client.dll' | Select-Object FullName, Length, CreationTime, LastWriteTime"
)
