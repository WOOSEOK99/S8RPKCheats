# SAM8RPK Ingame Cheat Build Script (PowerShell)
param (
    [Parameter(Mandatory = $true)]
    [ValidateSet("dinput8", "dxgi", "dwmapi", "all")]
    [string]$Type
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "   [$Type.dll] 빌드 시작" -ForegroundColor Cyan
Write-Host "========================================`n"

# 1. MSBuild 찾기
$msBuildPath = ""

# vswhere 경로 확인
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { 
    $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" 
}

# vswhere로 찾기
if (Test-Path $vswhere) {
    $msBuildPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
}

# 수동 경로 확인
if (-not $msBuildPath) {
    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($p in $fallbacks) {
        if (Test-Path $p) { 
            $msBuildPath = $p
            break 
        }
    }
}

if (-not $msBuildPath) {
    Write-Host "[오류] MSBuild.exe를 찾을 수 없습니다. Visual Studio 설치를 확인해주세요." -ForegroundColor Red
    return
}

Write-Host "사용 중인 MSBuild: $msBuildPath" -ForegroundColor Gray

# MSBuild가 출력을 UTF-8로 하도록 환경 변수 설정
$env:DOTNET_CLI_UI_LANGUAGE = "ko-KR"
$env:MSBuildLogNumberOfParameterErrors = "1"
# 아래 명령어가 핵심입니다 (출력 인코딩 강제)
$env:PYTHONIOENCODING = "utf-8"

# 2. 빌드 실행
$targets = @()
if ($Type -eq "all") {
    $targets = @("dinput8", "dxgi", "dwmapi")
}
else {
    $targets = @($Type)
}

foreach ($t in $targets) {
    Write-Host "`n>>> [$t.dll] 빌드 시도..." -ForegroundColor Yellow
    
    # 파워쉘의 호출 연산자(&)를 사용하여 실행
    # 명령어를 실행하기 직전에 chcp를 실행하도록 구성
cmd /c "chcp 65001 > nul && `"$msBuildPath`" `"SAM8RPK_Ingame_Cheat.sln`" /p:Configuration=Release /p:ProxyType=$t /p:TargetName=$t /m /v:m"

    if ($LASTEXITCODE -eq 0) {
        Write-Host "   [성공] $t.dll 빌드 완료" -ForegroundColor Green
    }
    else {
        Write-Host "   [실패] $t.dll 빌드 중 오류 발생 (ExitCode: $LASTEXITCODE)" -ForegroundColor Red
        if ($Type -eq "all") {
            Write-Host "전체 빌드를 중단합니다." -ForegroundColor Red
            return
        }
    }
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "   빌드 작업이 종료되었습니다." -ForegroundColor Green
Write-Host "========================================`n"
