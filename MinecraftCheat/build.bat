@echo off
setlocal enabledelayedexpansion
title VasyCheat Builder
chcp 65001 > nul

echo.
echo  ==========================================
echo   VasyCheat  ^|  Minecraft 1.12.2 Builder
echo  ==========================================
echo.

:: ── Проверяем зависимости ───────────────────────────────────────────────────
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] CMake не найден!
    echo     Скачай: https://cmake.org/download/
    pause & exit /b 1
)

where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Git не найден!
    echo     Скачай: https://git-scm.com/download/win
    pause & exit /b 1
)

:: ── Ищем Visual Studio ──────────────────────────────────────────────────────
set VS_GEN=
for %%G in (
    "Visual Studio 17 2022"
    "Visual Studio 16 2019"
) do (
    if "!VS_GEN!"=="" (
        cmake -G %%G --version >nul 2>&1 && set VS_GEN=%%G
    )
)

if "!VS_GEN!"=="" (
    echo [!] Visual Studio 2019/2022 не найдена!
    echo     Установи с https://visualstudio.microsoft.com/ (компонент "C++ desktop")
    pause & exit /b 1
)

echo [*] Генератор: !VS_GEN!

:: ── JAVA_HOME ───────────────────────────────────────────────────────────────
if "%JAVA_HOME%"=="" (
    echo [!] JAVA_HOME не задана!
    echo     Задай переменную окружения на папку JDK 8, например:
    echo     set JAVA_HOME=C:\Program Files\Java\jdk1.8.0_333
    echo.
    echo     Продолжаем — CMake попытается найти JDK автоматически...
    echo.
)

:: ── Создаём папку сборки ────────────────────────────────────────────────────
if not exist "build" mkdir build
cd build

echo [*] Конфигурирование CMake...
cmake .. -G !VS_GEN! -A x64 -DCMAKE_BUILD_TYPE=Release

if %errorlevel% neq 0 (
    echo.
    echo [!] CMake configuration failed!
    cd ..
    pause & exit /b 1
)

echo.
echo [*] Сборка Release...
cmake --build . --config Release --parallel

if %errorlevel% neq 0 (
    echo.
    echo [!] Build failed!
    cd ..
    pause & exit /b 1
)

cd ..

echo.
echo  ==========================================
echo   [+] Готово! Файлы в папке БИЛДЫ\:
echo  ==========================================
dir /b "..\БИЛДЫ\*.dll" "..\БИЛДЫ\*.exe" 2>nul
echo.
echo  Использование:
echo    1. Запусти Minecraft 1.12.2
echo    2. Запусти БИЛДЫ\Injector.exe  (от Администратора)
echo    3. В игре: RSHIFT = ClickGUI,  END = выгрузить
echo.
pause

