@echo off
setlocal EnableExtensions

:: Require Administrator privileges to remove C:\Windows\twain_32\sample2
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

set "DEST=C:\Windows\twain_32\sample2"

if not exist "%DEST%" (
    echo %DEST% is not installed.
    pause
    exit /b 0
)

echo This will remove:
echo   %DEST%
echo.
set /p "CONFIRM=Continue? [Y/N]: "
if /I not "%CONFIRM%"=="Y" (
    echo Cancelled.
    exit /b 0
)

:: Change working directory out of DEST in case uninstall.cmd is run from inside DEST
cd /d "%TEMP%"
rmdir /S /Q "%DEST%"

if exist "%DEST%" (
    echo.
    echo ERROR: Some files could not be removed. Close any scanner app or TWAIN bridge and try again.
    pause
    exit /b 1
)

echo.
echo Uninstalled successfully.
pause
endlocal
