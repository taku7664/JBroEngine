@echo off
rem Installs the upstream Code-OSS dependencies with the environment from upstream-env.cmd.
setlocal
call "%~dp0upstream-env.cmd"
if errorlevel 1 exit /b 1

node --version
echo SDK %WindowsSDKVersion%
echo VS  %vs2022_install%
echo LOCALAPPDATA %LOCALAPPDATA%

cd /d "%JBRO_EDITOR_ROOT%\upstream"
call npm ci
exit /b %errorlevel%
