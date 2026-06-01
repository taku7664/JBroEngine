@echo off
setlocal

set PROJECT_FILE=%~1
if "%PROJECT_FILE%"=="" set PROJECT_FILE=%JBRO_PROJECT_FILE%
if "%PROJECT_FILE%"=="" (
    echo Usage: build_web_debug.bat ^<Project.Jproject^>
    echo Or set JBRO_PROJECT_FILE to the project file path.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Debug -Clean
exit /b %ERRORLEVEL%
