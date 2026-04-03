@echo off
setlocal

echo Building SAM8RPK_Ingame_Cheat in Release mode...

:: 1. Use vswhere.exe to find MSBuild.exe automatically
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
    set "vswhere=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if exist "%vswhere%" (
    for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%i"
        goto :build
    )
)

:: 2. Fallback to common paths if vswhere fails
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    goto :build
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    goto :build
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    goto :build
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    goto :build
)

:: 3. Error handling if MSBuild isn't found
if "%MSBUILD%"=="" (
    echo [Error] MSBuild.exe could not be found. Please ensure Visual Studio is installed.
    pause
    exit /b 1
)

:build
echo MSBuild path: "%MSBUILD%"
"%MSBUILD%" SAM8RPK_Ingame_Cheat.sln /p:Configuration=Release

echo.
echo Build complete. Press any key to exit.
pause
