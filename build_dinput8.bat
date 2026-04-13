@echo off
chcp 65001 >nul
setlocal

echo.
echo ========================================
echo   [dinput8.dll] 빌드 시작
echo ========================================
echo.

:: MSBuild 자동 탐색 (vswhere 우선)
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" set "vswhere=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%vswhere%" (
    for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%i"
        goto :build
    )
)

:: Fallback 경로들
for %%p in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    "D:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
) do (
    if exist %%p ( set "MSBUILD=%%~p" & goto :build )
)

echo [오류] MSBuild.exe를 찾을 수 없습니다.
pause & exit /b 1

:build
echo MSBuild: "%MSBUILD%"
"%MSBUILD%" SAM8RPK_Ingame_Cheat.sln /p:Configuration=Release /p:ProxyType=dinput8 /p:TargetName=dinput8

if %ERRORLEVEL% neq 0 (
    echo.
    echo [실패] 빌드 오류가 발생했습니다.
    pause & exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo   [완료] dinput8.dll 빌드 성공!
echo   출력: x64\Release\dinput8.dll
echo ========================================
echo.
timeout /t 2 >nul
exit /b 0
