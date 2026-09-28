@echo off
rem Downloads Electron, compiles upstream Code-OSS and fetches its built-in extensions,
rem with the environment from upstream-env.cmd. Launching afterwards needs no rebuild:
rem set VSCODE_SKIP_PRELAUNCH=1 and run upstream\scripts\code.bat.
setlocal
call "%~dp0upstream-env.cmd"
if errorlevel 1 exit /b 1

cd /d "%JBRO_EDITOR_ROOT%\upstream"
node build/lib/preLaunch.ts
exit /b %errorlevel%
