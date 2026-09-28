@echo off
rem Launches the compiled upstream Code-OSS without rebuilding, with its user data and
rem extensions kept under .toolchain so that a dev run does not touch the home profile.
rem argv.json still goes to %%USERPROFILE%%\.vscode-oss-dev: its location ignores --user-data-dir.
setlocal
call "%~dp0upstream-env.cmd"
if errorlevel 1 exit /b 1

set VSCODE_SKIP_PRELAUNCH=1
call "%JBRO_EDITOR_ROOT%\upstream\scripts\code.bat" --user-data-dir "%JBRO_TOOLCHAIN%\user-data" --extensions-dir "%JBRO_TOOLCHAIN%\extensions" %*
exit /b %errorlevel%
