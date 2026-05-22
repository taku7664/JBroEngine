@echo off
setlocal

pushd "%~dp0\..\.."

if not exist Build\Web\Debug mkdir Build\Web\Debug

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
    -O0 ^
    -gsource-map ^
    --use-port=emdawnwebgpu ^
    -sASSERTIONS=1 ^
    -sALLOW_MEMORY_GROWTH=1 ^
    -sASYNCIFY=1 ^
    %PRELOAD_ARGS% ^
    --shell-file PlatformBuild\Web\shell.html ^
    -o Build\Web\Debug\index.html

if errorlevel 1 (
    echo Web debug build failed.
    popd
    exit /b 1
)

echo Web debug build completed: Build\Web\Debug\index.html
popd
endlocal
