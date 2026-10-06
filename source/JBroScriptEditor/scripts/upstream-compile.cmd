@echo off
rem Recompiles upstream Code-OSS and its built-in extensions after patches change the sources.
rem preLaunch compiles only when upstream\out is missing (build/lib/preLaunch.ts), so it cannot be used here.
rem compile-copilot is left out: extensions/copilot is compiled once by preLaunch and its sources are not patched.
setlocal
call "%~dp0upstream-env.cmd"
if errorlevel 1 exit /b 1

cd /d "%JBRO_UPSTREAM%"
call npm run compile-client
exit /b %errorlevel%
