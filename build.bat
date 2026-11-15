@echo off
setlocal

:: Force terminate existing process to avoid file locks
taskkill /f /im AuraWeather.exe >nul 2>nul

:: Check for compiler
where g++ >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] g++ not found in PATH!
    pause
    exit /b 1
)

if exist build rmdir /s /q build
mkdir build
if exist AuraWeather.exe del AuraWeather.exe

echo [1/2] Compiling resources...
windres.exe -I resources -i resources\app.rc -o build\app_res.o --codepage=65001
if errorlevel 1 (
    echo [ERROR] Resource compilation failed!
    pause
    exit /b 1
)

echo [2/2] Compiling AURA Weather...
g++.exe -std=c++20 -O2 -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
  src\main.cpp src\common.cpp src\http.cpp src\services.cpp src\settings.cpp src\ui.cpp ^
  build\app_res.o -o AuraWeather.exe ^
  -static -static-libgcc -static-libstdc++ ^
  -Wl,-Bstatic -lssl -lcrypto -lz -lstdc++ -lwinpthread ^
  -lws2_32 -lcrypt32 -ladvapi32 -luserenv -lole32 -luuid -lcomctl32 -lgdiplus -lshlwapi ^
  -lshell32 -ldwmapi -luser32 -lgdi32 -lwinmm -limm32 -lversion -lbcrypt

if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo.
echo =========================================
echo Build OK: AuraWeather.exe
echo =========================================
exit /b 0