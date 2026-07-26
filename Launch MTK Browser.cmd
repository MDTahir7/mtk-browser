@echo off
rem Starts the native MTK Browser. Prefers the staged release folder and
rem falls back to the development build output.
setlocal

set "STAGED=%~dp0dist\MTK Browser\MTKBrowser.exe"
set "DEV=%~dp0build\native-adblock\native\Release\MTKBrowser.exe"

if exist "%STAGED%" (
    start "" "%STAGED%"
    goto :eof
)

if exist "%DEV%" (
    start "" "%DEV%"
    goto :eof
)

echo MTK Browser is not built yet.
echo Run: cmake --build build/native-adblock --config Release --target mtk_browser
pause
