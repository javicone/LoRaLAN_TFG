@echo off
setlocal

set "BASE=%~dp0"
set "SRC=%BASE%LoRaLAN"
set "OUT=%BASE%LoRaLAN.zip"

if not exist "%SRC%" (
  echo ERROR: No se encontro la carpeta "%SRC%".
  exit /b 1
)

if exist "%OUT%" del /f /q "%OUT%"

powershell -NoProfile -Command "Compress-Archive -Path \"%SRC%\" -DestinationPath \"%OUT%\" -Force"
if errorlevel 1 (
  echo ERROR: Fallo al crear el ZIP.
  exit /b 1
)

echo OK: ZIP creado en "%OUT%".
