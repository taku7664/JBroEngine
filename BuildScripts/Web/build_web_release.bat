@echo off
setlocal

set PROJECT_FILE=%~1
if "%PROJECT_FILE%"=="" set PROJECT_FILE=%JBRO_PROJECT_FILE%
set EMSDK_ROOT=%~2
if "%EMSDK_ROOT%"=="" set EMSDK_ROOT=%EMSDK%
if "%PROJECT_FILE%"=="" (
    echo Usage: build_web_release.bat ^<Project.Jproject^> [EmsdkRoot]
    echo Or set JBRO_PROJECT_FILE to the project file path.
    exit /b 1
)

if "%EMSDK_ROOT%"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Release -Clean
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0\..\BuildWeb.ps1" -Project "%PROJECT_FILE%" -Configuration Release -Clean -EmsdkRoot "%EMSDK_ROOT%"
)
exit /b %ERRORLEVEL%
