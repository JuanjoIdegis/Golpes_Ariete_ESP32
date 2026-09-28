@echo off
title Compilador ESP32
cd /d "%~dp0"

echo =======================================================
echo   COMPILANDO FIRMWARE ESP32 CON ARDUINO CLI...
echo =======================================================

"%~dp0arduino-cli.exe" compile --fqbn esp32:esp32:esp32 --output-dir "%~dp0." "%~dp0esp32_cycle_controller"

if exist "%~dp0esp32_cycle_controller.ino.merged.bin" (
    copy /Y "%~dp0esp32_cycle_controller.ino.merged.bin" "%~dp0esp32_cycle_controller.merged.bin" >nul
    echo.
    echo =======================================================
    echo   ¡EXITO! BINARIO GENERADO EN:
    echo   %~dp0esp32_cycle_controller.ino.merged.bin
    echo =======================================================
) else (
    echo.
    echo [!] No se pudo ubicar el archivo compilado. Revisa si hubo errores.
)

echo.
pause
