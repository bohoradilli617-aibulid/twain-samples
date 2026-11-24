@echo off
setlocal EnableExtensions

:: Require Administrator privileges to write to C:\Windows\twain_32\sample2
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"
set "DEST=C:\Windows\twain_32\sample2"

echo Installing 32-bit Windows Virtual Scanner to:
echo   %DEST%
echo.

if not exist "%DEST%" (
    mkdir "%DEST%"
    if errorlevel 1 (
        echo ERROR: Could not create %DEST%.
        pause
        exit /b 1
    )
)

set "HAD_SOURCE_JSON=0"
if exist "%DEST%\source.json" (
    set "HAD_SOURCE_JSON=1"
    copy /Y "%DEST%\source.json" "%TEMP%\vs_source_backup.json" >nul
)

xcopy "%SRC%\*" "%DEST%\" /E /I /Y /Q >nul
if errorlevel 1 (
    echo.
    echo ERROR: Failed to copy files. Make sure any scanner app or TWAIN bridge is closed and try again.
    pause
    exit /b 1
)

if "%HAD_SOURCE_JSON%"=="1" (
    copy /Y "%TEMP%\vs_source_backup.json" "%DEST%\source.json" >nul
    del /Q "%TEMP%\vs_source_backup.json" >nul 2>&1
    echo Preserved existing %DEST%\source.json.
)

:: Remove install.cmd from destination (keep uninstall.cmd for convenience)
if exist "%DEST%\install.cmd" del /Q "%DEST%\install.cmd" >nul 2>&1

echo.
echo Installation complete!
echo   Installed to: %DEST%
echo   Config file:  %DEST%\source.json
echo.

if not exist "C:\Windows\twain_32.dll" if not exist "C:\Windows\SysWOW64\twaindsm.dll" if not exist "C:\Windows\System32\twaindsm.dll" (
    echo WARNING: No 32-bit TWAIN Data Source Manager ^(twain_32.dll / twaindsm.dll^) was detected.
    echo TWAIN apps require a 32-bit DSM before they can list this scanner.
    echo.
)

pause
endlocal
