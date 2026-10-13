@echo off
rem Launches the compiled upstream Code-OSS without rebuilding, with its user data and
rem extensions kept under .toolchain so that a dev run does not touch the home profile.
rem argv.json still goes to %%USERPROFILE%%\.vscode-oss-dev: its location ignores --user-data-dir.
rem Set JBRO_EDITOR_USER_DATA for an isolated test profile; do not repeat --user-data-dir on the CLI.
setlocal
rem Reject profile overrides before starting Electron: repeated scalar CLI options become arrays.
for %%A in (%*) do (
	for /f "tokens=1 delims==" %%B in ("%%~A") do (
		if /i "%%B"=="--user-data-dir" goto bad_user_data
		if /i "%%B"=="--extensions-dir" goto bad_extensions
	)
)
call "%~dp0upstream-env.cmd"
if errorlevel 1 exit /b 1

set VSCODE_SKIP_PRELAUNCH=1
if not defined JBRO_EDITOR_USER_DATA set "JBRO_EDITOR_USER_DATA=%JBRO_TOOLCHAIN%\user-data"
call "%JBRO_UPSTREAM%\scripts\code.bat" --user-data-dir "%JBRO_EDITOR_USER_DATA%" --extensions-dir "%JBRO_TOOLCHAIN%\extensions" --extensionDevelopmentPath="%~dp0..\extensions\jbro-engine" --extensionDevelopmentPath="%~dp0..\extensions\jbro-languages" %*
exit /b %errorlevel%

:bad_user_data
echo upstream-launch: set JBRO_EDITOR_USER_DATA instead of passing --user-data-dir
exit /b 1

:bad_extensions
echo upstream-launch: extensions-dir is managed by this launcher
exit /b 1
