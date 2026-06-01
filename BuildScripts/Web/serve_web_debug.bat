@echo off
setlocal

set PACKAGE_DIR=%~1
if "%PACKAGE_DIR%"=="" set PACKAGE_DIR=%JBRO_WEB_PACKAGE_DIR%
if "%PACKAGE_DIR%"=="" (
    echo Usage: serve_web_debug.bat ^<WebPackageDir^>
    echo Or set JBRO_WEB_PACKAGE_DIR to the Web package folder.
    exit /b 1
)
if not exist "%PACKAGE_DIR%\index.html" (
    echo Web package index.html was not found: "%PACKAGE_DIR%"
    exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
    echo python was not found. Install Python or serve the Web package folder with another local server.
    exit /b 1
)

pushd "%PACKAGE_DIR%"
echo Open http://localhost:8080/index.html
python -m http.server 8080
popd
