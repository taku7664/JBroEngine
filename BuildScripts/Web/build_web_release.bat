@echo off
setlocal

pushd "%~dp0\..\.."

if not exist Build\Web\Release mkdir Build\Web\Release

set PRELOAD_ARGS=
if exist Assets (
    set PRELOAD_ARGS=--preload-file Assets@/Assets
)

where emcc >nul 2>nul
if errorlevel 1 (
    echo emcc was not found. Run emsdk_env.bat first.
    popd
    exit /b 1
)

emcc @BuildScripts\Web\web_debug_sources.rsp ^
    -I. ^
    -IEngine ^
    -IEngine\ThirdParty ^
    -IUtillity ^
    -std=c++20 ^
    -DJBRO_PLATFORM_WEB ^
    -DJBRO_GAME ^
    -O2 ^
    --use-port=emdawnwebgpu ^
    -sASSERTIONS=0 ^
    -sALLOW_MEMORY_GROWTH=1 ^
    -sASYNCIFY=1 ^
    %PRELOAD_ARGS% ^
    --shell-file PlatformBuild\Web\shell.html ^
    -o Build\Web\Release\index.html

if errorlevel 1 (
    echo Web release build failed.
    popd
    exit /b 1
)

echo Web release build completed: Build\Web\Release\index.html
popd
endlocal
