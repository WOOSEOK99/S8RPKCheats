@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "PYTHON=py"
) else (
    set "PYTHON=python"
)

%PYTHON% -m PyInstaller --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PyInstaller is not installed.
    echo Run: %PYTHON% -m pip install pyinstaller
    pause
    exit /b 1
)

%PYTHON% -m PyInstaller --noconfirm --clean --onefile --windowed --name S8RPK_Trait_INI_Editor S8RPK_Trait_INI_Editor.py
if errorlevel 1 (
    echo [ERROR] EXE build failed.
    pause
    exit /b 1
)

echo.
echo EXE: %~dp0dist\S8RPK_Trait_INI_Editor.exe
pause
endlocal
