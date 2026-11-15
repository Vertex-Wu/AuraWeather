@echo off
setlocal

:: 检查是否安装了 g++ 编译器
where g++ >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] g++ not found in PATH!
    echo Please install MSYS2 or MinGW, and make sure g++ is added to your system PATH.
    pause
    exit /b 1
)

:: 检查 windres（用于编译图标资源）
where windres >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] windres not found in PATH!
    echo Please ensure your MinGW environment is fully configured.
    pause
    exit /b 1
)

if not exist build mkdir build

echo [1/2] Compiling resources...
windres.exe -I resources -i resources\app.rc -o build\app_res.o --codepage=65001
if errorlevel 1 exit /b 1

echo [2/2] Compiling AURA Weather...
rem Important: keep symbols (do not pass -s) to avoid Kaspersky false positives.
g++.exe -std=c++20 -O2 -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
  src\main.cpp src\common.cpp src\http.cpp src\services.cpp src\settings.cpp src\ui.cpp ^
  build\app_res.o -o AuraWeather.exe ^
  -static -static-libgcc -static-libstdc++ ^
  -Wl,-Bstatic -lssl -lcrypto -lz -lstdc++ -lwinpthread ^
  -lws2_32 -lcrypt32 -ladvapi32 -luserenv -lole32 -luuid -lcomctl32 -lgdiplus -lshlwapi ^
  -lshell32 -ldwmapi -luser32 -lgdi32 -lwinmm -limm32 -lversion -lbcrypt
if errorlevel 1 exit /b 1

echo.
echo Build OK:
for %%F in (AuraWeather.exe) do echo   %%~fF  %%~zF bytes
exit /b 0