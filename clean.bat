@echo off
setlocal
echo Cleaning project for backup...

:: 1. Delete all folders named "x64", "Debug", or "Release" recursively
for /d /r . %%d in (x64 Debug Release) do (
    if exist "%%d" (
        echo Deleting: "%%d"
        rd /s /q "%%d"
    )
)

:: 2. Delete the .vs folder (Intellisense database and local settings)
if exist ".vs" (
    echo Deleting: .vs
    rd /s /q ".vs"
)

:: 3. Delete common temporary files
echo Cleaning temporary files...
del /s /q /f *.user 2>nul
del /s /q /f *.suo 2>nul
del /s /q /f *.db 2>nul
del /s /q /f *.ipch 2>nul
del /s /q /f *.log 2>nul
del /s /q /f *.tlog 2>nul

echo.
echo Cleanup complete! The folder size should be much smaller now.
echo You can now ZIP or backup the project safely.
pause
