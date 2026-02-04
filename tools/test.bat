@echo off
echo Testing ParentalMonitor Driver
echo =================================

REM Check if running as administrator
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Please run as Administrator!
    pause
    exit /b 1
)

REM Enable test signing
echo Enabling test signing...
bcdedit /set testsigning on

REM Copy driver
echo Copying driver...
copy "build\x64\Release\ParentalMonitor.sys" "C:\Windows\System32\drivers\" >nul

REM Create service
echo Creating service...
sc create ParentalMonitor binpath= "C:\Windows\System32\drivers\ParentalMonitor.sys" type= kernel start= auto error= normal

if errorlevel 1 (
    echo Failed to create service!
    goto :cleanup
)

REM Start service
echo Starting service...
sc start ParentalMonitor

if errorlevel 1 (
    echo Failed to start service!
    goto :cleanup
)

REM Check service status
echo Checking service status...
sc query ParentalMonitor

echo.
echo Driver installed and started successfully!
echo.
echo To test persistence, reboot and check if driver loads automatically.
echo Use DebugView from Sysinternals to see debug messages.
echo.
pause
goto :eof

:cleanup
echo Cleaning up...
sc stop ParentalMonitor 2>nul
sc delete ParentalMonitor 2>nul
pause