@echo off
setlocal

set "PROJECT_FILE=%~1"
if "%PROJECT_FILE%"=="" set "PROJECT_FILE=%JBRO_PROJECT_FILE%"
set "EMSDK_ROOT=%~2"
if "%EMSDK_ROOT%"=="" set "EMSDK_ROOT=%EMSDK%"
set "OUTPUT_ROOT=%~3"
if "%OUTPUT_ROOT%"=="" set "OUTPUT_ROOT=%JBRO_OUTPUT_ROOT%"
if "%PROJECT_FILE%"=="" (
    echo Usage: build_web_debug.bat ^<Project.Jproject^> [EmsdkRoot] [OutputRoot]
    echo Or set JBRO_PROJECT_FILE to the project file path.
    exit /b 1
)

if "%EMSDK_ROOT%"=="" (
    if "%OUTPUT_ROOT%"=="" (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Debug -Clean
    ) else (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Debug -Clean -OutputRoot "%OUTPUT_ROOT%"
    )
) else (
    if "%OUTPUT_ROOT%"=="" (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Debug -Clean -EmsdkRoot "%EMSDK_ROOT%"
    ) else (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Debug -Clean -EmsdkRoot "%EMSDK_ROOT%" -OutputRoot "%OUTPUT_ROOT%"
    )
)
exit /b %ERRORLEVEL%
