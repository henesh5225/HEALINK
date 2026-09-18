@echo off
setlocal

rem Stage the HEALINK SX1278 SPI configuration into the Raspberry Pi 4 QNX BSP.
rem Optional argument: BSP root. When omitted, the normal user workspace path is used.

set "PROJECT_ROOT=%~dp0.."
set "BSP_ROOT=%~1"
if "%BSP_ROOT%"=="" set "BSP_ROOT=%USERPROFILE%\ide-8.0.3-workspace\hw.raspberrypi-bcm2711-rpi4"

set "SRC=%PROJECT_ROOT%\bsp_config\spi\spi.conf"
set "DST=%BSP_ROOT%\install\etc\system\config\spi\spi.conf"

if not exist "%SRC%" (
  echo ERROR: Source config not found:
  echo   %SRC%
  exit /b 1
)

if not exist "%BSP_ROOT%" (
  echo ERROR: BSP root not found:
  echo   %BSP_ROOT%
  echo Pass the BSP root as the first argument.
  exit /b 1
)

if not exist "%BSP_ROOT%\install\etc\system\config\spi" mkdir "%BSP_ROOT%\install\etc\system\config\spi"

copy /Y "%SRC%" "%DST%"
if errorlevel 1 exit /b 1

echo.
echo SX1278 SPI config staged successfully:
echo   %DST%
echo.
echo Rebuild the QNX Raspberry Pi image from the BSP workspace before booting.
exit /b 0
