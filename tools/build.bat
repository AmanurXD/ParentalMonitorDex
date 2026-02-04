@echo off
echo Building ParentalMonitorDex Kernel Driver
echo =========================================

REM Set toolchain paths
SET WDK_PATH=C:\Program Files (x86)\Windows Kits\10
SET SDK_PATH=%WDK_PATH%
SET BUILD_ARCH=x64
SET CONFIGURATION=Release
SET DRIVER_NAME=ParentalMonitorDex

REM Detect SDK version from environment if available (e.g., Windows 11 SDK)
SET SDK_VER=%WindowsSDKLibVersion%
IF NOT DEFINED SDK_VER (
    SET SDK_VER=10.0.19041.0
) ELSE (
    REM Trim trailing backslash if present
    IF "%SDK_VER:~-1%"=="\" SET SDK_VER=%SDK_VER:~0,-1%
)

REM Create output directories
if not exist "build\%BUILD_ARCH%\%CONFIGURATION%" mkdir "build\%BUILD_ARCH%\%CONFIGURATION%"

REM Compile sources
echo Compiling sources...
cl.exe /nologo /c /Zl /Zp8 /Gz /GS- /kernel /W4 /WX /O2 /Oi /Oy /GF /Gy ^
      /wd4324 /D _WIN64 /D _AMD64_ /D WINVER=0x0A00 /D _WIN32_WINNT=0x0A00 ^
      /D NTDDI_VERSION=0x0A000000 /D DBG=1 /D DEBUG=1 ^
      /I "%SDK_PATH%\Include\%SDK_VER%\km" ^
      /I "%SDK_PATH%\Include\%SDK_VER%\shared" ^
      /I "..\driver\src" ^
      /Fo"build\%BUILD_ARCH%\%CONFIGURATION%\\" ^
      ..\driver\src\pmx.c

if errorlevel 1 goto :error

REM Link driver
echo Linking driver...
link.exe /NOLOGO /DRIVER /RELEASE /SUBSYSTEM:NATIVE /VERSION:10.0 ^
         /OUT:"build\%BUILD_ARCH%\%CONFIGURATION%\%DRIVER_NAME%.sys" ^
         /NODEFAULTLIB /MANIFEST:NO /DEBUG /PDB:"build\%BUILD_ARCH%\%CONFIGURATION%\%DRIVER_NAME%.pdb" ^
         /BASE:0x10000 /ENTRY:DriverEntry ^
         /INCREMENTAL:NO /OPT:REF /OPT:ICF /MERGE:_TEXT=.text;_PAGE=PAGE ^
         /SECTION:INIT,d /IGNORE:4197,4017,4037,4039,4065,4070,4078,4087,4089,4221 ^
         /LIBPATH:"%WDK_PATH%\lib\%SDK_VER%\km\%BUILD_ARCH%" ^
         ntoskrnl.lib hal.lib wmilib.lib ndis.lib ntstrsafe.lib ^
         build\%BUILD_ARCH%\%CONFIGURATION%\*.obj

if errorlevel 1 goto :error

REM Copy INF file
copy "..\driver\ParentalMonitorDex.inf" "build\%BUILD_ARCH%\%CONFIGURATION%\" >nul

echo.
echo Build successful!
echo Driver: build\%BUILD_ARCH%\%CONFIGURATION%\%DRIVER_NAME%.sys
goto :eof

:error
echo Build failed!
pause
