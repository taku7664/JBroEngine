@echo off
rem Sets up the environment for building the upstream Code-OSS checkout in %JBRO_EDITOR_WORK%\upstream.
rem Call it from another script with `call`, then run npm there. It changes the caller's environment.
rem
rem Why each line is here is in tasks/ide-plan.md section 4.2.
rem
rem  - JBRO_EDITOR_WORK: the folder that holds upstream\ and .toolchain\. It lives outside the engine
rem    repo: the checkout and its caches take about 9 GB, more than the drive of the engine repo has free.
rem    There is no fallback next to this script, so that a missing variable cannot fill that drive.
rem  - vcvars64 with SDK 10.0.22621.0: node-gyp picks the newest registered Windows SDK, and on this
rem    machine 10.0.26100.0 is installed without its headers (specstrings_strict.h is missing).
rem    node-gyp honours the SDK of an existing developer environment.
rem  - vs2022_install: build/npm/preinstall.ts only looks under "Microsoft Visual Studio\2022" and "\2019".
rem  - LOCALAPPDATA, TEMP, TMP, npm_config_cache: keep every download and cache off C:.
rem    LOCALAPPDATA must move as a whole rather than pointing node-gyp somewhere else on its own:
rem    preinstall.ts overlays custom headers into %LOCALAPPDATA%\node-gyp\Cache, and node-gyp finds
rem    its cache through env-paths, which reads the same variable. Moving only one of them makes the
rem    overlay skip silently.
rem  - The Spectre-mitigated libraries (MSB8040) are a Visual Studio component and cannot be set here.

if not defined JBRO_EDITOR_WORK (
	echo upstream-env: set JBRO_EDITOR_WORK to the folder that holds upstream\ and .toolchain\
	exit /b 1
)
for %%I in ("%JBRO_EDITOR_WORK%") do set "JBRO_EDITOR_WORK=%%~fI"
set "JBRO_UPSTREAM=%JBRO_EDITOR_WORK%\upstream"
set "JBRO_TOOLCHAIN=%JBRO_EDITOR_WORK%\.toolchain"

if not exist "%JBRO_UPSTREAM%\package.json" (
	echo upstream-env: no upstream Code-OSS checkout at %JBRO_UPSTREAM%
	exit /b 1
)

if not exist "%JBRO_TOOLCHAIN%\node\node.exe" (
	echo upstream-env: portable Node is missing at %JBRO_TOOLCHAIN%\node
	exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo upstream-env: vswhere.exe not found
	exit /b 1
)
set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
	echo upstream-env: no Visual Studio with the C++ x64 toolset
	exit /b 1
)

rem vcvars looks vswhere.exe up on PATH and prints an error when it is not there.
for %%I in ("%VSWHERE%") do set "PATH=%%~dpI;%PATH%"
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" 10.0.22621.0 >nul
if errorlevel 1 (
	echo upstream-env: vcvars64.bat failed
	exit /b 1
)

set "vs2022_install=%VS_INSTALL%"
set "PATH=%JBRO_TOOLCHAIN%\node;%PATH%"
set "LOCALAPPDATA=%JBRO_TOOLCHAIN%\localappdata"
set "TEMP=%JBRO_TOOLCHAIN%\temp"
set "TMP=%JBRO_TOOLCHAIN%\temp"
set "npm_config_cache=%JBRO_TOOLCHAIN%\npm-cache"

if not exist "%LOCALAPPDATA%" mkdir "%LOCALAPPDATA%"
if not exist "%TEMP%" mkdir "%TEMP%"
if not exist "%npm_config_cache%" mkdir "%npm_config_cache%"

exit /b 0
