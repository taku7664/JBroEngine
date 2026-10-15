@echo off
rem JBro 씬 에디터 실행. 이 폴더(Localization, ThirdParty)를 기준으로 연다.
cd /d "%~dp0"
start "" "JBroEditorHost.exe" %*
