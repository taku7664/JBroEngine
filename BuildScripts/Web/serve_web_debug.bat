@echo off
setlocal

pushd "%~dp0\..\..\Build\Web\Debug"

where python >nul 2>nul
if errorlevel 1 (
    echo python was not found. Install Python or serve Build\Web\Debug with another local server.
    popd
    exit /b 1
)

echo Open http://localhost:8080/index.html
python -m http.server 8080

popd
endlocal
